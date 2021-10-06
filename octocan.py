#!/usr/bin/env python3

import sys
import time
import subprocess
import pathlib
import numpy as np

import skin
#import rospy
#from sensor_msgs.msg import Joy

# List of devices to try
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1']
baud_rate = 2000000

num_patches = 8
num_cells = 16

threshold = 10

# Exponential smoothing (0,1]. 1=no smoothing
alpha = 0.1

# Calibration profile to use
profile = 'demo-profile.csv'

# Value range of joints
joint_range = {
    0: (-0.5, 0.5),  # shoulder_yaw
    1: (-0.3, 0.5),  # shoulder_pitch
    3: (-0.5, 0.5),  # upper_arm_roll
    4: (-0.3, 0.5),  # elbow_pitch
}

def status(*args):
    print(*args)


def fatal(*args):
    print(*args, file=sys.stderr)
    sys.exit(1)


def setup_octocan():
    # Find octocan device
    device_found = False
    for device in devices:
        path = pathlib.Path(device)
        if path.exists() and path.is_char_device():
            device_found = device
            break
    if not device_found:
        fatal("Cannot find octocan device, tried:", *devices, sep='\n')
    else:
        status("Found octocan device on", device)

    # Configure serial
    def run_stty(*args):
        result = subprocess.run(['stty', '-F', device] + list(args), check=True)
    status("Configuring", device)
    try:
        run_stty('raw')
        run_stty('-echo', '-echoe', '-echok')
        run_stty(str(baud_rate))
    except subprocess.CalledProcessError:
        fatal("Error configuring", device)

    # Setup sensor communication object
    sensor = skin.Skin(patches=num_patches, cells=num_cells, device=device, history=10)
    sensor.set_alpha(alpha)
    if profile:
        sensor.read_profile(profile)
    return sensor

total_polls = 0
values = np.zeros((num_cells,))

def poll_status(sensor):
    for patch in range(1, sensor.patches + 1): # patch IDs start at 1
        for cell in range(sensor.cells):
            values[cell] = sensor.get_expavg(patch, cell)
        m = values[values != 0].mean()
        print(' %10.0f %s' % (m, 'O' if m > threshold else '.'), end='')
    print()

    global total_polls
    total_polls += 1


def main():
    octocan = setup_octocan()
    octocan.start()

    while True:
        time.sleep(0.1)
        poll_status(octocan)

    # # Set up ROS node
    # rospy.init_node('octocan')
    # rate = rospy.Rate(100)
    # pub = rospy.Publisher('/joy', Joy, queue_size=1)
    # joy = Joy()

    # # Publish data to ROS
    # while not rospy.is_shutdown():
    #     joy.axes = joints.tolist()
    #     pub.publish(joy)
    #     rate.sleep()


# joints = np.zeros((5,), dtype=float)
# def read_state():
#     global joints
#     values = np.zeros((sensor.cells,))
#     for patch in range(1, sensor.patches + 1): # patch IDs start at 1
#         for cell in range(sensor.cells):
#             values[cell] = sensor.get_expavg(patch, cell)

    # joints[0] = 
    # joints[1] = 
    # joints[3] = 
    # joints[4] = 

if __name__ == '__main__':
    main()
#EOF
