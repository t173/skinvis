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
    button_rows = 1
    fig = plt.figure(constrained_layout=False, figsize=(3, 0.4*(heat_rows + cellline_rows*num_cells + button_rows)), facecolor='lightgray')
    gs = fig.add_gridspec(
        nrows=heat_rows + num_cells*cellline_rows + button_rows, ncols=1,
        hspace=0, wspace=0, right=0.85,
        left=0.1, top=0.95, bottom=0.05,
    )
    heat = fig.add_subplot(gs[:heat_rows, 0])
    cell_ax = [fig.add_subplot(gs[heat_rows + i*cellline_rows, 0]) for i in range(num_cells)]
    tare_ax = fig.add_subplot(gs[-button_rows, 0])
    tare_button = Button(tare_ax, 'Tare')
    tare_button.label.set_fontsize(14)
    tare_button.on_clicked(lambda _: calibrate(sensor))

    # textbox_axs = [fig.add_subplot(gs[heat_rows + i*cellline_rows, 1]) for i in range(num_cells)]
    # calib = sensor.get_patch_profile(patch)
    # textboxes = {}
    # for i in range(num_cells):
    #     textbox = TextBox(textbox_axs[i], 'c1 =', initial='%g' % calib['c1'][i], color='gray', hovercolor='lightgray')
    #     textbox.on_submit(lambda txt: textbox_submit(sensor, patch, i, txt))
    #     textboxes[i] = textbox

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

    history = np.zeros((num_cells, cmdline.history))
    history_pos = 0
    history[:, history_pos] = state

    for cell_id in patch_layout:
        pos = patch_layout[cell_id]
        heat.text(pos[0], pos[1], str(cell_id), ha='center', va='center', color='gray')

    cell_lines = []
    for i, ax in enumerate(cell_ax):
        ax.tick_params(left=False, right=True, top=False,
                       bottom=False, labelleft=False, labelright=True,
                       labelbottom=False, labeltop=False)
        ax.set_ylabel(str(i), color='dimgray', rotation=0, ha='center', va='center', labelpad=10)
        line, = ax.plot(np.arange(cmdline.history), history[i, :], color='k')
        ax.set_xlim(0, cmdline.history)
        cell_lines.append(line)

    args = {
        'sensor': sensor,
        'patch': patch,
        'heat': heat,
        'cell_ax': cell_ax,
        'cell_lines': cell_lines,
        'cmap': cmap,
        'collection': collection,
        'cell_to_poly': cell_to_poly,
        'history': history,
        'history_pos': history_pos,
        'tare': tare_button,
        #'textboxes': textboxes,
    }
    return fig, args

# state_max = None
def anim_update(frame, args):
    # global state_max
    # state = sensor.get_state()
    # if state_max is None:
    #     state_max = state
    # else:
    #     state_max = np.maximum(state_max, state)

    patch = args['patch']
    state = args['sensor'].get_patch_state(patch)

    args['history_pos'] = (args['history_pos'] + 1) % cmdline.history

    args['history'][:, args['history_pos']] = state
    args['collection'].set_array(state)

    pos = (args['history_pos'] + 1) % cmdline.history
    for c in range(len(args['cell_lines'])):
        h = args['history'][c, :]
        line = args['cell_lines'][c]
        xdata, ydata = line.get_data()
        line.set_data(xdata, np.hstack([h[pos:], h[:pos]]))
        hmin = h.min()
        hmax = h.max()
        if hmin == hmax:
            hmin -= 100
            hmax += 100
        args['cell_ax'][c].set_ylim(hmin, hmax)

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

    # User side patch numbers start with 1
    fig, args = anim_init(sensor, cmdline.patch)
    anim = animation.FuncAnimation(fig, cache_frame_data=False, func=anim_update, fargs=(args,), interval=cmdline.delay)
    
    # plt.figure(figsize=(1,1))
    # ax = plt.axes()
    # button = Button(ax, 'Tare')
    # button.label.set_fontsize(24)
    # button.on_clicked(lambda _: calibrate(sensor))

    sensor.start()
    if not cmdline.nocalibrate:
        calibrate(sensor)

    plt.show()

    shutdown = True
    sensor.stop()
    stats_thread.join()

    #print(state_max)

if __name__ == '__main__':
    main()
#EOF
