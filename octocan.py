#!/usr/bin/env python3

import skin
import rospy
import numpy as np
import threading
from sensor_msgs.msg import Joy

device = '/dev/ttyUSB0'
alpha = 0.1
profile = None

# Value range of joints
joint_range = {
    0: (-0.5, 0.5),  # shoulder_yaw
    1: (-0.3, 0.5),  # shoulder_pitch
    3: (-0.5, 0.5),  # upper_arm_roll
    4: (-0.3, 0.5),  # elbow_pitch
}

# Setup octocan skin sensor
sensor = skin.Skin(patches=8, cells=16, device=device, history=10)
sensor.set_alpha(alpha)
if profile:
    sensor.read_profile(profile)

joints = np.zeros((5,), dtype=float)
def read_state():
    global joints
    values = np.zeros((sensor.cells,))
    for patch in range(1, sensor.patches + 1): # patch IDs start at 1
        for cell in range(sensor.cells):
            values[cell] = sensor.get_expavg(patch, cell)
        if values.mean() > threshold

    # joints[0] = 
    # joints[1] = 
    # joints[3] = 
    # joints[4] = 

# Set up ROS node
rospy.init_node('octocan')
rate = rospy.Rate(100)
pub = rospy.Publisher('/joy', Joy, queue_size=1)
joy = Joy()

# Publish data to ROS
while not rospy.is_shutdown():
    joy.axes = joints.tolist()
    pub.publish(joy)
    rate.sleep()
