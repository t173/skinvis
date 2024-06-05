#!/usr/bin/env python3

import sys

import skin

def read_cmdline():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--device', '-d', default='/dev/ttyACM0', help="serial device to use")
    parser.add_argument('--layout', '-l', default='octocan2.layout', help="layout configuration")
    parser.add_argument('--calib', '-c', default='octocan2.calib', help="calibration profile")
    parser.add_argument('--debug', help="write debugging log to file")
    return parser.parse_args()

def calibrate(sensor):
    import time
    sensor.calibrate_start()
    print('Baseline calibration... DO NOT TOUCH!')
    time.sleep(4)
    sensor.calibrate_stop()
    print('Baseline calibration finished')

def main():
    # Verify device exists and is a character device
    import pathlib
    path = pathlib.Path(cmdline.device)
    if not path.exists():
        raise ValueError("Device does not exist: " + cmdline.device)
    if not path.is_char_device():
        raise ValueError("Device is not a character device: " + cmdline.device)

    # Configure serial using external tool
    import subprocess
    try:
        subprocess.run(['stty', '-clocal', '-F', cmdline.device,
                        'raw', '-echo', '-echoe', '-echok', '115200'])
    except subprocess.CalledProcessError:
        print("Error configuring", cmdline.device, file=sys.stderr)
        sys.exit(1)

    # Create a new Skin instance
    sensor = skin.Skin(device=cmdline.device, layout=cmdline.layout)

    # Set value smoothing (alpha: 1=none, 0=never change)
    sensor.set_alpha(0.8)

    # Set pressure calculation smoothing
    sensor.set_pressure_alpha(0.1)

    # Load calibration profile
    sensor.read_profile(cmdline.calib)

    # [Optional] Write to debugging log
    if cmdline.debug:
        sensor.debuglog(cmdline.debug)

    # Can ask Skin object for its layout. This is a dict where key is
    # patch ID, value is another dict of cell ID to x,y position
    layout = sensor.get_layout()
    from pprint import pprint
    print("Layout is")
    pprint(layout)

    # Start reading
    try:
        sensor.start()
        while True:
            for patch in layout:
                print("Patch", patch, "=", sensor.get_patch_state(patch))
    finally:
        sensor.stop()


if __name__ == '__main__':
    global cmdline
    cmdline = read_cmdline()
    main()
#EOF
