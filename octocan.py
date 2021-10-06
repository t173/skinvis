#!/usr/bin/env python3

import sys
import time
import subprocess
import pathlib
import numpy as np

import skin
import rospy
from sensor_msgs.msg import Joy

# List of devices to try
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1']
baud_rate = 2000000
num_patches = 8
num_cells = 16
alpha = 0.01  # (0, 1] where 1 is no smoothing

profile = 'demo-profile.csv'
threshold = 100

# ROS rate in Hertz
ROS_rate = 100

# Value range of joints
joint_range = {
    0: (-0.5, 0.5),  # shoulder_yaw
    1: (-0.1, 0.5),  # shoulder_pitch
    3: (-0.5, 0.5),  # upper_arm_roll
    4: (-0.3, 0.5),  # elbow_pitch
}
joint_direction = { j: 1 for j in joint_range.keys() }

joints = np.zeros((max(joint_range.keys()),))

# Increment this much (radians) per ROS poll
joint_increment = 0.0005


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

def increment_joint(joint):
    global joints, joint_direction
    joints[joint] += joint_direction[joint]*joint_increment
    if (joint_direction[joint] > 0 and joints[joint] >= joint_range[joint][1]) \
       or (joint_direction[joint] < 0 and joints[joint] <= joint_range[joint][0]):
        joint_direction[joint] = -joint_direction[joint]

def poll_octocan(sensor):
    global total_polls
    total_polls += 1

    for patch in range(1, sensor.patches + 1): # patch IDs start at 1
        for cell in range(sensor.cells):
            values[cell] = sensor.get_expavg(patch, cell)
        m = values[values != 0].mean()

        if patch == 2 and m > threshold:
            increment_joint(1)
        print(' %10.0f %s' % (m, 'O' if m > threshold else '.'), end='')
    print()


def calibrate(sensor):
    sensor.calibrate_start()
    status('Baseline calibration... DO NOT TOUCH!')
    time.sleep(4)
    sensor.calibrate_stop()
    status('Baseline calibration finished')

def main():
    octocan = setup_octocan()
    octocan.start()
    calibrate(octocan)

    # Set up ROS node
    rospy.init_node('octocan')
    rate = rospy.Rate(ROS_rate)
    pub = rospy.Publisher('/joy', Joy, queue_size=1)
    joy = Joy()

    # Publish data to ROS
    while not rospy.is_shutdown():
        poll_octocan(octocan)
        joy.axes = joints.tolist()
        pub.publish(joy)
        rate.sleep()

if __name__ == '__main__':
    main()
#EOF
