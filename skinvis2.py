#!/usr/bin/env python3
#
# Visualization for octocan prototype v2
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

import skin

# List of devices to try (if not given on cmdline)
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1']
baud_rate = 2000000  # default, overrideable at cmdline

shutdown = False
total_frames = 0

# Rotate 90 because patches are mounted sideways on octocan
placement = np.rot90(np.array([
    [1, 2, 10,  9],
    [3, 4, 12, 11],
    [6, 5, 13, 14],
    [8, 7, 15, 16],
]) - 1)

pos_to_cell = { p: c for p, c in enumerate(placement.ravel()) }
cell_to_pos = { c: p for p, c in enumerate(placement.ravel()) }

CIRCLE_SCALE = 500
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
    parser.add_argument('--baud', '-b', type=int, default=baud_rate, help='use baud rate')
    parser.add_argument('--alpha', type=float, default=0.8)
    parser.add_argument('--pressure_alpha', type=float, default=0.1)
    parser.add_argument('--patches', type=int, default=8)
    parser.add_argument('--cells', type=int, default=16)
    parser.add_argument('--profile', metavar='CSVFILE', default='profile.csv', help='dynamic range calibration from CSVFILE')
    parser.add_argument('--debug', help='write debugging log')
    parser.add_argument('--figsize', metavar=('WIDTH', 'HEIGHT'), type=float, nargs=2, default=(8, 5), help='set figure size in inches')
    parser.add_argument('--delay', type=float, default=30, help='delay between plot updates in milliseoncds')
    parser.add_argument('--vmin', type=float, default=-40)#, default=-75000)
    parser.add_argument('--vmax', type=float, default=40)#, default=75000)
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
        return subprocess.run(['stty', '-F', device] + list(args), check=True)
    print("Configuring", device)
    try:
        run_stty('raw')
        run_stty('-echo', '-echoe', '-echok')
        run_stty(str(cmdline.baud))
    except subprocess.CalledProcessError:
        print("Error configuring", device, file=sys.stderr)
        sys.exit(1)

    # Setup sensor communication object
    sensor = skin.Skin(device=device, patches=cmdline.patches, cells=cmdline.cells)
    sensor.set_alpha(cmdline.alpha)
    sensor.set_pressure_alpha(cmdline.pressure_alpha)
    if cmdline.profile:
        sensor.read_profile(cmdline.profile)
    return sensor

def start_calibrate_button():
    def calibrate_button(sensor):
        calibrate(sensor)
        if cmdline.style == 'text':
            df, _ = args
            for patch in range(cmdline.patches):
                for cell in range(cmdline.cells):
                    current = int(sensor.get_expavg(patch, cell))
                    df['min'] = df['max'] = current
                    df['batch'] = df['count'] = 0

    plt.figure(figsize=(1,1))
    ax = plt.axes()
    button = mpl.widgets.Button(ax, 'Tare')
    button.label.set_fontsize(24)
    button.on_clicked(lambda _: calibrate_button(sensor))

def stats_updater(sensor, view, sleep=2):
    global shutdown, total_frames
    bytes_before = sensor.total_bytes
    records_before = sensor.total_records
    before = datetime.datetime.now()
    num_cells = sensor.patches*sensor.cells
    frames_before = total_frames
    dropped_before = sensor.dropped_records
    misalign_before = sensor.misalignments
    while not shutdown:
        time.sleep(sleep)
        now = datetime.datetime.now()
        bytes_now = sensor.total_bytes
        records_now = sensor.total_records
        frames_now = total_frames
        dropped_now = sensor.dropped_records
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

num_rows = 2
num_cols = 4

def anim_init(sensor):
    fig, axs = plt.subplots(num_rows, num_cols)
    fig.set_figwidth(cmdline.figsize[0])
    fig.set_figheight(cmdline.figsize[1])

    margin = 0.05
    plt.subplots_adjust(left=margin, right=1-margin, bottom=margin, top=1-margin, wspace=0.01, hspace=0.01)
    for ax in axs.flatten():
        ax.axis('off')

    vmin, vmax = cmdline.vmin, cmdline.vmax
    norm = mpl.colors.Normalize(vmin=vmin, vmax=vmax, clip=True)
    cmap = mpl.colors.LinearSegmentedColormap.from_list("cardinal", [
        [0.00, 'black'],
        [0.01, '#AD0000'],
        [0.45, 'white'],
        [0.55, 'white'],
        [0.99, '#AD0000'],
        [1.00, 'black'],
    ])
    mapper = mpl.cm.ScalarMappable(norm=norm, cmap=cmap)
    
    ims = []
    circles = []
    state = sensor.get_state()
    for patch in range(sensor.patches):
        row = patch//num_cols
        col = patch % num_cols
        ax = axs[row, col]
        A = np.array(state[patch])[placement]
        ims.append(ax.imshow(A, norm=norm, cmap=cmap, vmin=cmdline.vmin, vmax=cmdline.vmax, zorder=1))
        circles.append(ax.scatter([0], [0], s=1, zorder=10, **CIRCLE_PROPS))
    return fig, ims, circles

np.set_printoptions(formatter={'float': lambda x: "{0:8.3f}".format(x)})

def anim_update(frame, sensor, ims, circles):
    state = sensor.get_state()
    for patch in range(sensor.patches):
        A = np.absolute(np.array(state[patch])[placement])#.clip(max=cmdline.vmax)
        pressure = sensor.get_patch_pressure(patch)
        # if patch == 1:
        #     print(pressure)
        circles[patch].set_offsets([pressure[1:]])
        if pressure[0] >= 0.2:
            circles[patch].set_sizes([max(1, CIRCLE_SCALE*pressure[0])])
        else:
            circles[patch].set_sizes([0.001])
        ims[patch].set_data(A)

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

    fig, ims, circles = anim_init(sensor)
    anim = animation.FuncAnimation(fig, func=anim_update, fargs=(sensor, ims, circles), interval=cmdline.delay)
    
    start_calibrate_button()
    sensor.start()
    calibrate(sensor)

    plt.show()

    shutdown = True
    sensor.stop()
    stats_thread.join()

if __name__ == '__main__':
    main()
#EOF

