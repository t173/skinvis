#!/usr/bin/env python3
#
# Manual calibration and tuning for octocan prototype v2
#
# Bryan Harris
# bryan.harris.1@louisville.edu

import sys
import pathlib
import subprocess
import time
import datetime
import argparse
import threading
import numpy as np
import pandas as pd
import matplotlib as mpl
import matplotlib.pyplot as plt
import matplotlib.animation as animation

from matplotlib.patches import Polygon
from matplotlib.widgets import Button, TextBox
from scipy.spatial import Voronoi

import skin

mpl.rcParams['toolbar'] = 'None'

# List of devices to try (if not given on cmdline)
devices = ['/dev/ttyUSB0']
baud_rate = 115200  # default, overrideable at cmdline

shutdown = False
total_frames = 0

CIRCLE_SCALE = 2
CIRCLE_PROPS = {
    'edgecolor': 'cadetblue',
    'facecolor': None,
    'lw': 2,
    'alpha': 0.8,
}

def parse_cmdline():
    global cmdline
    parser = argparse.ArgumentParser()
    parser.add_argument('--device')
    parser.add_argument('--layout', default="octocan.layout")
    parser.add_argument('--baud', '-b', type=int, default=baud_rate, help='use baud rate')
    parser.add_argument('--alpha', type=float, default=0.8)
    parser.add_argument('--pressure_alpha', type=float, default=0.1)
    parser.add_argument('--patch', '-p', type=int, default=1)
    parser.add_argument('--profile', metavar='CSVFILE', default='octocan3_test.calib', help='dynamic range calibration from CSVFILE')
    parser.add_argument('--log', metavar='CSV', help='log data to CSV file')
    parser.add_argument('--debug', help='write debugging log (for developer)')
    parser.add_argument('--history', type=int, default=100, help='line plot history size')
    parser.add_argument('--delay', type=float, default=0, help='delay between plot updates in milliseoncds')
    parser.add_argument('--nocalibrate', action='store_true', default=True, help='do not perform baseline calibration on startup')
    parser.add_argument('--noconfigure', action='store_true', help='do not configure serial')
    cmdline = parser.parse_args()

cell_lbl_props = {
    'color': 'dimgray',
    'rotation': 0,
    #'labelpad': 10,
}

class CellLine:
    def __init__(self, sensor, ax, label, initial_value, color='k', **kwargs):
        self.ax = ax
        ax.axis('off') # gives 10x frame rate!!!
        ax.set_xlim(0, cmdline.history)
        self.values = np.full(cmdline.history, initial_value)
        self.sensor = sensor
        self.pos = 0
        self.automode = True
        self.editor = None
        self.target = sensor.get_target_pressure()
        
        self.line, = ax.plot(np.arange(cmdline.history), self.values, color=color, **kwargs)
        x, y, width, height = ax.get_position().bounds

        textcolor = 'dimgray'
        margin = 0.05
        self.cell_text = plt.figtext(x - margin, y + 0.5*height, label, ha='center', va='center', fontsize=12, color=textcolor)
        self.upper_value = initial_value
        self.lower_value = initial_value
        upper_lbl = '%.0f' % initial_value
        lower_lbl = '%.0f' % initial_value
        hmargin = 0.01
        vmargin = 0.005
        self.upper_text = plt.figtext(x + width + hmargin, y + height - vmargin, upper_lbl, ha='left', va='top', color=textcolor)
        self.lower_text = plt.figtext(x + width + hmargin, y + vmargin, lower_lbl, ha='left', va='bottom', color=textcolor)

    def add(self, value):
        self.values[self.pos] = value
        self.pos += 1
        self.pos %= len(self.values)
        self.update_minmax()
        xdata, _ = self.line.get_data()
        ydata = np.hstack([self.values[self.pos:], self.values[:self.pos]])
        if not self.automode:
            ydata = np.clip(ydata, 0, self.target)
        self.line.set_data(xdata, ydata)
        if self.editor:
            self.editor.update(value)

    def fmt(self, value):
        return '%.0f' % value
        
    def update_minmax(self):
        if self.automode:
            vmin = self.values.min()
            vmax = self.values.max()
            if vmax != self.upper_value:
                self.upper_value = vmax
                self.upper_text.set_text(self.fmt(vmax))
            if vmin != self.lower_value:
                self.lower_value = vmin
                self.lower_text.set_text(self.fmt(vmin))
            self.ax.set_ylim(vmin, vmax)

    def install(self, ed):
        self.editor = ed
            
    def uninstall(self):
        self.editor = None
            
    def set_auto_mode(self):
        self.automode = True
        self.update_minmax()
    
    def set_target_mode(self):
        self.automode = False
        low = 0
        high = self.target
        self.lower_value = low
        self.upper_value = high
        self.lower_text.set_text(self.fmt(low))
        self.upper_text.set_text(self.fmt(high))
        self.ax.set_ylim(low, high)


class AvgLine(CellLine):
    def __init__(self, sensor, ax, label, initial_value, color='#AD0000', **kwargs):
        super().__init__(sensor, ax, label, initial_value, color, **kwargs)
        
    def add(self, values):
        super().add(self.sensor.get_patch_mean(cmdline.patch))

class PressureLine(CellLine):
    def __init__(self, sensor, ax, label, initial_value, color='cadetblue', **kwargs):
        super().__init__(sensor, ax, label, initial_value, color, **kwargs)


class ParamEditor:
    def __init__(self, sensor, ax, cell, cell_line):
        self.sensor = sensor
        self.patch_profile = sensor.get_patch_profile(cmdline.patch)
        self.ax = ax
        self.cell = cell
        initval = self.patch_profile['c1'][cell]
        self.textbox = TextBox(ax, '%d' % cell, initial=str(initval), label_pad=0.05, textalignment='left')
        self.textbox.label.set_fontsize(12)
        self.textbox.on_submit(self.set_c1)
        self.mx_mode = False
        self.target = sensor.get_target_pressure()
        self.cell_line = cell_line

        x, y, w, h = ax.get_position().bounds
        mx_ax = ax.get_figure().add_axes([x + w, y, h, h])
        self.mx_button = Button(mx_ax, '\u224F')#'\u229E')
        self.mx_button.label.set_fontsize(18)
        self.mx_button.on_clicked(lambda _: self.toggle_mx())

    def set_c1(self, value_str):
        try:
            value = float(value_str)
            self.sensor.set_c1(cmdline.patch, self.cell, value)
            print("Set cell %d c1 = %g" % (self.cell, value))
        except ValueError:
            print("Invalid value:", value_str)
        self.textbox.set_val(self.sensor.get_c1(cmdline.patch, self.cell))

    def enter_mx(self):
        self.cell_line.install(self)
        self.textbox.color = '#AD6666'
        self.textbox.hovercolor = '#AD0000'
        self.mx_mode = True
        self.mx_max = 0
        self.textbox.stop_typing()
        #self.textbox.active = False

    def exit_mx(self):
        self.cell_line.uninstall()
        old_c1 = self.sensor.get_c1(cmdline.patch, self.cell)
        sign = -1 if old_c1 < 0 else 1
        if self.mx_max != 0:
            c1 = sign*self.target/self.mx_max
            self.set_c1(c1)
            print("Max seen was", self.mx_max, " and now using new c1 =", c1)
        else:
            print("Ignoring zero max for cell", self.cell)
        self.textbox.color = '0.95'
        self.textbox.hovercolor = '1'
        self.mx_mode = False
        #self.textbox.active = True

    def update(self, value):
        """
        Receive value from associated CellLine
        """
        if self.mx_mode and value > self.mx_max:
            self.mx_max = value
        
    def toggle_mx(self):
        """
        mx_mode is where we use the maximum value for automatically setting gain
        """
        if self.mx_mode:
            self.exit_mx()
        else:
            self.enter_mx()
        #self.ax.get_figure().canvas.draw_idle()
        #self.ax.redraw_in_frame()

def setup_octocan():
    # Find octocan device
    device_found = False
    for device in [cmdline.device] if cmdline.device else devices:
        path = pathlib.Path(device)
        if path.exists() and path.is_char_device():
            device_found = device
            break
    if not device_found:
        print("Cannot find octocan device, tried:", *devices, sep='\n', file=sys.stderr)
        sys.exit(1)
    else:
        print("Found octocan device on", device)

    # Configure serial
    def run_stty(*args):
        return subprocess.run(['stty', '-clocal', '-F', device] + list(args), check=True)
    if not cmdline.noconfigure:
        print("Configuring", device)
        try:
            run_stty('raw', '-echo', '-echoe', '-echok', str(cmdline.baud))
        except subprocess.CalledProcessError:
            print("Error configuring", device, file=sys.stderr)
            sys.exit(1)

    # Setup sensor communication object
    sensor = skin.Skin(device=device, layout=cmdline.layout)
    sensor.set_alpha(cmdline.alpha)
    sensor.set_pressure_alpha(cmdline.pressure_alpha)
    if cmdline.profile:
        sensor.read_profile(cmdline.profile)
    if cmdline.log:
        sensor.log(cmdline.log)
    return sensor

def stats_updater(sensor, view, sleep=2):
    global shutdown, total_frames
    bytes_before = sensor.total_bytes
    records_before = sensor.total_records
    before = datetime.datetime.now()
    num_cells = sensor.total_cells
    frames_before = total_frames
    def dropped_records():
        tally = sensor.get_record_tally()
        return tally['patch_outofrange'] + tally['cell_outofrange']
    dropped_before = dropped_records()
    misalign_before = sensor.misalignments
    while not shutdown:
        time.sleep(sleep)
        now = datetime.datetime.now()
        bytes_now = sensor.total_bytes
        records_now = sensor.total_records
        frames_now = total_frames
        dropped_now = dropped_records()
        misalign_now = sensor.misalignments

        time_delta = (now - before).total_seconds()
        bytes_rate = (bytes_now - bytes_before)/time_delta
        records_rate = (records_now - records_before)/time_delta
        total_Hz = records_rate/num_cells
        frame_rate = (frames_now - frames_before)/time_delta

        print("reader: %.2f KB/s (%d misaligns)  %.0f records/s (%d dropped)  %.1f Hz   plotter: %.1f fps" % (
            bytes_rate/1024,
            misalign_now - misalign_before,
            records_rate,
            dropped_now - dropped_before,
            total_Hz,
            frame_rate
        ))

        bytes_before = bytes_now
        records_before = records_now
        frames_before = frames_now
        dropped_before = dropped_now
        misalign_before = misalign_now
        before = now

def tessellate(sensor, patch):
    layout = sensor.get_layout()
    pl = layout[patch]
    cell_ids = np.array(list(pl.keys()))
    cell_pos = np.array([pl[c] for c in cell_ids])
    points = cell_pos

    xmin = points[:,0].min()
    xmax = points[:,0].max()
    ymin = points[:,1].min()
    ymax = points[:,1].max()

    def margin(pnts):
        if len(pnts) <= 1:
            return 1
        return np.diff(np.sort(np.unique(pnts))).mean()

    xmargin = margin(points[:,0])
    ymargin = margin(points[:,1])
    boundary = np.array(
        [[xmin - 2*xmargin, ymin - 2*ymargin],
         [xmin - 2*xmargin, ymax + 2*ymargin],
         [xmax + 2*xmargin, ymin - 2*ymargin],
         [xmax + 2*xmargin, ymax + 2*ymargin]])
    lims = np.array([
        [points[:,0].min() - 0.5*xmargin, points[:,1].min() - 0.5*ymargin],
        [points[:,0].max() + 0.5*xmargin, points[:,1].max() + 0.5*ymargin]
    ])
    vor = Voronoi(np.vstack([points, boundary]))
    polys = [
        None if region == [] or -1 in region
        else Polygon(vor.vertices[region])
        for region in vor.regions
    ]
    cell_to_poly = { cell_ids[i]: polys[vor.point_region[i]] for i in range(len(cell_ids)) }
    return cell_to_poly, lims

in_auto_mode = True
def toggle_mode(cell_lines):
    global in_auto_mode
    in_auto_mode = not in_auto_mode
    for cl in cell_lines:
        if in_auto_mode:
            cl.set_auto_mode()
        else:
            cl.set_target_mode()

def anim_init(sensor, patch):
    patch_layout = sensor.get_layout()[patch]
    num_cells = len(patch_layout)
    cell_to_poly, lims = tessellate(sensor, patch)

    heat_rows = 6
    cellline_rows = 1
    agg_rows = 2
    button_rows = 1
    total_rows = heat_rows + cellline_rows*num_cells + agg_rows + button_rows
    fig = plt.figure(num="Patch %d" % patch, constrained_layout=False, figsize=(3, 0.4*total_rows), facecolor='w')#'lightgray')
    gs = fig.add_gridspec(
        nrows=total_rows, ncols=1,
        hspace=0.1, wspace=0, right=0.75,
        left=0.125, top=0.95, bottom=0.05,
    )
    heat = fig.add_subplot(gs[:heat_rows, 0])
    cell_axs = [fig.add_subplot(gs[heat_rows + i*cellline_rows, 0]) for i in range(num_cells)]
    avg_ax = fig.add_subplot(gs[heat_rows + num_cells*cellline_rows, 0])
    pressure_ax = fig.add_subplot(gs[heat_rows + num_cells*cellline_rows + 1, 0])
    tare_ax = fig.add_subplot(gs[-button_rows, 0])
    tare_button = Button(tare_ax, 'Tare')
    tare_button.label.set_fontsize(14)
    tare_button.on_clicked(lambda _: calibrate(sensor))

    heat.axis('off')
    heat.set_xlim(*lims[:,0])
    heat.set_ylim(*lims[:,1])
    heat.set_aspect('equal')

    target_pressure = sensor.get_target_pressure()
    norm = mpl.colors.Normalize(vmin=-target_pressure, vmax=target_pressure, clip=True)
    cmap = mpl.colors.LinearSegmentedColormap.from_list("cardinal", [
        [0.00, 'black'],
        [0.01, '#AD0000'],
        [0.45, 'white'],
        [0.55, 'white'],
        [0.99, '#AD0000'],
        [1.00, 'black'],
    ])

    cell_ids = sorted(list(cell_to_poly.keys()))
    polys = [ cell_to_poly[i] for i in cell_ids ]
    collection = mpl.collections.PatchCollection(polys, cmap=cmap, norm=norm)

    state = sensor.get_patch_state(patch)
    collection.set_array(state)
    heat.add_collection(collection)

    for cell_id in patch_layout:
        pos = patch_layout[cell_id]
        heat.text(pos[0], pos[1], str(cell_id), ha='center', va='center', color='gray', fontsize=14)

    cell_labels = sensor.get_cell_ids(patch)
    cell_lines = [ CellLine(sensor, ax, cell_labels[i], state[i]) for i, ax in enumerate(cell_axs) ]
    avg_line = AvgLine(sensor, avg_ax, 'x\u0305', np.mean(state))
    avg_line.target = sensor.get_target_pressure()

    magnitude, _, _ = sensor.get_patch_pressure(patch)
    pressure_line = PressureLine(sensor, pressure_ax, 'M', magnitude)
    
    tx, ty, tw, th = tare_ax.get_position().bounds
    mode_ax = fig.add_axes([tx + tw, ty, 2*th, th])
    mode_button = Button(mode_ax, '\u2195')
    mode_button.label.set_fontsize(16)
    mode_button.on_clicked(lambda _, cl=cell_lines + [avg_line]: toggle_mode(cl))

    circle = heat.scatter([0], [0], s=1, zorder=10, edgecolor='cadetblue', facecolor=None, lw=2, alpha=0.8)
    
    global args
    args = {
        'sensor': sensor,
        'patch': patch,
        'heat': heat,
        'cell_axs': cell_axs,
        'cell_lines': cell_lines,
        'cmap': cmap,
        'collection': collection,
        'cell_to_poly': cell_to_poly,
        'avg_line': avg_line,
        'tare_button': tare_button,
        'mode_button': mode_button,
        'circle': circle,
        'pressure_line': pressure_line,
    }
    return fig

def anim_update(frame):
    global args
    patch = args['patch']
    sensor = args['sensor']

    state = sensor.get_patch_state(patch)
    args['collection'].set_array(state)

    for i, cl in enumerate(args['cell_lines']):
        cl.add(state[i])
    args['avg_line'].add(state)

    magnitude, x, y = sensor.get_patch_pressure(patch)
    args['pressure_line'].add(magnitude)
    
    circle = args['circle']
    circle.set_offsets([x, y])
    if magnitude < 10:
        circle.set_sizes([0.001])
    else:
        circle.set_sizes([max(1, 2*magnitude)])

    global total_frames
    total_frames += 1

def calibrate(sensor, keep=True, show=True):
    sensor.calibrate_start()
    print('Baseline calibration... DO NOT TOUCH!')
    time.sleep(4)
    sensor.calibrate_stop()
    print('Baseline calibration finished')

def save_profile(sensor):
    print('Saving calibration profile to', cmdline.profile)
    sensor.save_profile(cmdline.profile)

def tune_table(sensor, cell_lines, patch):
    patch_layout = sensor.get_layout()[patch]
    num_cells = len(patch_layout)
    patch_profile = sensor.get_patch_profile(patch)
    
    fig = plt.figure(num="Tune Patch %d" % cmdline.patch, constrained_layout=False,
                     figsize=(3, 0.4*(num_cells + 1)), facecolor='lightgray')
    gs = fig.add_gridspec(
        nrows=num_cells + 1, ncols=1,
        hspace=0.1, wspace=0, right=0.8,
        left=0.2, top=0.95, bottom=0.05,
    )
    cell_ids = sensor.get_cell_ids(patch)

    editors = []
    for i, cell_id in enumerate(cell_ids):
        ax = fig.add_subplot(gs[i, 0])
        editor = ParamEditor(sensor, ax, cell_id, cell_lines[i])
        cell_lines[i].install(editor)
        editors.append(editor)
        
    save_ax = fig.add_subplot(gs[-1, 0])
    save_button = Button(save_ax, 'Save to file') #\U0001F5AB #\u2193
    save_button.label.set_fontsize(12)
    save_button.on_clicked(lambda _, s=sensor: save_profile(s))
    
    return editors, save_ax, save_button


def main():
    global shutdown
    parse_cmdline()
    sensor = setup_octocan()

    if cmdline.debug:
        sensor.debuglog(cmdline.debug)

    stats_thread = threading.Thread(target=stats_updater, args=(sensor, None))
    stats_thread.start()

    print(sensor.get_target_pressure())
    sensor.start()
    if not cmdline.nocalibrate:
        calibrate(sensor)

    global args
    fig = anim_init(sensor, cmdline.patch)
    anim = animation.FuncAnimation(fig, cache_frame_data=False, func=anim_update, interval=cmdline.delay)

    tt = tune_table(sensor, args['cell_lines'], cmdline.patch)
    plt.show()

    shutdown = True
    sensor.stop()
    stats_thread.join()

if __name__ == '__main__':
    main()
#EOF
