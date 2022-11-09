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
import pandas
import matplotlib as mpl
import matplotlib.pyplot as plt

import skin

# List of devices to try (if not given on cmdline)
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1']
baud_rate = 2000000  # default, overrideable at cmdline

shutdown = False
total_frames = 0

def parse_cmdline():
    global cmdline
    parser = argparse.ArgumentParser()
    parser.add_argument('--device')
    parser.add_argument('--baud', '-b', type=int, default=baud_rate, help='use baud rate')
    parser.add_argument('--alpha', type=float, default=1)
    parser.add_argument('--patches', type=int, default=8)
    parser.add_argument('--cells', type=int, default=16)
    parser.add_argument('--profile', metavar='CSVFILE', help='dynamic range calibration from CSVFILE')
    parser.add_argument('--debug', help='write debugging log')
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


def main():
    global shutdown
    parse_cmdline()
    sensor = setup_octocan()

    if cmdline.debug:
        sensor.debuglog(cmdline.debug)

    stats_thread = threading.Thread(target=stats_updater, args=(sensor, None))
    stats_thread.start()

    start_calibrate_button()
    sensor.start()
    plt.show()

    shutdown = True
    sensor.stop()
    stats_thread.join()

if __name__ == '__main__':
    main()
#EOF

