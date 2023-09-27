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
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1', '/dev/ttyACM0', '/dev/ttyACM1']
baud_rate = 2000000  # default, overrideable at cmdline

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
    parser.add_argument('--profile', metavar='CSVFILE', default='profile.csv', help='dynamic range calibration from CSVFILE')
    parser.add_argument('--debug', help='write debugging log')
    parser.add_argument('--history', type=int, default=100, help='line plot history size')
    parser.add_argument('--delay', type=float, default=0, help='delay between plot updates in milliseoncds')
    parser.add_argument('--nocalibrate', action='store_true', help='do not perform baseline calibration on startup')
    parser.add_argument('--noconfigure', action='store_true', help='do not configure serial')
    cmdline = parser.parse_args()

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
            run_stty('raw')
            run_stty('-echo', '-echoe', '-echok')
            run_stty(str(cmdline.baud))
        except subprocess.CalledProcessError:
            print("Error configuring", device, file=sys.stderr)
            sys.exit(1)

    # Setup sensor communication object
    sensor = skin.Skin(device=device, layout=cmdline.layout)
    sensor.set_alpha(cmdline.alpha)
    sensor.set_pressure_alpha(cmdline.pressure_alpha)
    if cmdline.profile:
        sensor.read_profile(cmdline.profile)
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

def textbox_submit(sensor, patch, cell, text):
    print("submit:", patch, cell, text)
    plt.draw()

def anim_init(sensor, patch):
    patch_layout = sensor.get_layout()[patch]
    num_cells = len(patch_layout)
    cell_to_poly, lims = tessellate(sensor, patch)

    heat_rows = 6
    cellline_rows = 1
    agg_rows = 1
    button_rows = 1
    total_rows = heat_rows + cellline_rows*num_cells + agg_rows + button_rows
    fig = plt.figure(constrained_layout=False, figsize=(3, 0.4*total_rows), facecolor='w')#'lightgray')
    gs = fig.add_gridspec(
        nrows=total_rows, ncols=1,
        hspace=0.1, wspace=0, right=0.75,
        left=0.125, top=0.95, bottom=0.05,
    )
    heat = fig.add_subplot(gs[:heat_rows, 0])
    cell_axs = [fig.add_subplot(gs[heat_rows + i*cellline_rows, 0]) for i in range(num_cells)]
    avg_ax = fig.add_subplot(gs[heat_rows + num_cells*cellline_rows, 0])
    tare_ax = fig.add_subplot(gs[-button_rows, 0])
    tare_button = Button(tare_ax, 'Tare')
    tare_button.label.set_fontsize(14)
    tare_button.on_clicked(lambda _: calibrate(sensor))

    heat.axis('off')
    heat.set_xlim(*lims[:,0])
    heat.set_ylim(*lims[:,1])
    heat.set_aspect('equal')

    #margin = 0.05
    #plt.subplots_adjust(left=margin, right=1-margin, bottom=margin, top=1-margin, wspace=0, hspace=0)

    norm = mpl.colors.Normalize(vmin=-100, vmax=100, clip=True)
    cmap = mpl.colors.LinearSegmentedColormap.from_list("cardinal", [
        [0.00, 'black'],
        [0.01, '#AD0000'],
        [0.45, 'white'],
        [0.55, 'white'],
        [0.99, '#AD0000'],
        [1.00, 'black'],
    ])
    #mapper = mpl.cm.ScalarMappable(norm=norm, cmap=cmap)

    cell_ids = sorted(list(cell_to_poly.keys()))
    polys = [ cell_to_poly[i] for i in cell_ids ]
    collection = mpl.collections.PatchCollection(polys, cmap=cmap, norm=norm)

    state = sensor.get_patch_state(patch)
    collection.set_array(state)
    heat.add_collection(collection)

    history = np.zeros((num_cells + 1, cmdline.history))
    history_pos = 0
    history[:num_cells, history_pos] = state
    history[-1, history_pos] = history[:num_cells, history_pos].mean()

    for cell_id in patch_layout:
        pos = patch_layout[cell_id]
        heat.text(pos[0], pos[1], str(cell_id), ha='center', va='center', color='gray', fontsize=14)

    cell_lines = []
    cell_labels = sensor.get_cell_ids(patch)
    
    cell_lbl_props = {
        'color': 'dimgray',
        'rotation': 0,
        #'labelpad': 10,
    }

    def configure_ax(ax, lbl, values, **kwargs):
        ax.axis('off') # gives 10x frame rate!!!
        ax.set_xlim(0, cmdline.history)
        line, = ax.plot(np.arange(cmdline.history), values, **kwargs)
        x, y, width, height = ax.get_position().bounds
        margin = 0.05
        plt.figtext(x - margin, y + 0.5*height, lbl, ha='center', va='center', fontsize=12, **cell_lbl_props)
        upper_lbl = '%.0f' % values.max()
        lower_lbl = '%.0f' % values.min()
        hmargin = 0.01
        vmargin = 0.005
        upper = plt.figtext(x + width + hmargin, y + height - vmargin, upper_lbl, ha='left', va='top', **cell_lbl_props)
        lower = plt.figtext(x + width + hmargin, y + vmargin, lower_lbl, ha='left', va='bottom', **cell_lbl_props)
        return line, upper, lower

    cell_lines = []
    lowers = []
    uppers = []
    for i, ax in enumerate(cell_axs):
        line, upper, lower = configure_ax(ax, cell_labels[i], history[i, :], color='k')
        cell_lines.append(line)
        uppers.append(upper)
        lowers.append(lower)
    avg_line, avg_upper, avg_lower = configure_ax(avg_ax, 'x\u0305', history[-1, :], color='#ad0000')

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
        'history': history,
        'history_pos': history_pos,
        'tare': tare_button,
        'avg_ax': avg_ax,
        'avg_line': avg_line,
        'uppers': uppers,
        'lowers': lowers,
        'avg_upper': avg_upper,
        'avg_lower': avg_lower,
    }
    return fig

def anim_update(frame):
    global args
    patch = args['patch']

    state = args['sensor'].get_patch_state(patch)
    args['history'][:-1, args['history_pos']] = state
    args['history'][-1, args['history_pos']] = args['history'][:-1, args['history_pos']].mean()
    args['collection'].set_array(state)

    args['history_pos'] = (args['history_pos'] + 1) % cmdline.history
    pos = args['history_pos']
    for c in range(len(args['cell_lines'])):
        h = args['history'][c, :]
        line = args['cell_lines'][c]
        xdata, ydata = line.get_data()
        hmin = h.min()
        hmax = h.max()
        line.set_data(xdata, np.hstack([h[pos:], h[:pos]]))
        if hmin == hmax:
            hmin -= 100
            hmax += 100
        args['cell_axs'][c].set_ylim(hmin, hmax)
        args['uppers'][c].set_text('%.0f' % hmax)
        args['lowers'][c].set_text('%.0f' % hmin)

    avg = args['history'][-1, :]
    avg_line = args['avg_line']
    xdata, ydata = avg_line.get_data()
    avg_line.set_data(xdata, np.hstack([avg[pos:], avg[:pos]]))
    avg_min = avg.min()
    avg_max = avg.max()
    args['avg_ax'].set_ylim(avg_min, avg_max)
    args['avg_upper'].set_text('%.0f' % avg_max)
    args['avg_lower'].set_text('%.0f' % avg_min)

    global total_frames
    total_frames += 1

def calibrate(sensor, keep=True, show=True):
    sensor.calibrate_start()
    print('Baseline calibration... DO NOT TOUCH!')
    time.sleep(4)
    sensor.calibrate_stop()
    print('Baseline calibration finished')
            
def main():
    global shutdown
    parse_cmdline()
    sensor = setup_octocan()

    if cmdline.debug:
        sensor.debuglog(cmdline.debug)

    stats_thread = threading.Thread(target=stats_updater, args=(sensor, None))
    stats_thread.start()

    sensor.start()
    if not cmdline.nocalibrate:
        calibrate(sensor)

    fig = anim_init(sensor, cmdline.patch)
    anim = animation.FuncAnimation(fig, cache_frame_data=False, func=anim_update, interval=cmdline.delay)
    
    plt.show()

    shutdown = True
    sensor.stop()
    stats_thread.join()

if __name__ == '__main__':
    main()
#EOF
