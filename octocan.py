#!/usr/bin/env python3

import rospy
import numpy as np
from sensor_msgs.msg import Joy
#from std_msgs.msg import Float32MultiArray


joints = np.zeros((4,), dtype=float)


if __name__ == '__main__':
    rospy.init_node('octocan')
    rate = rospy.Rate(100)
    pub = rospy.Publisher('/joy', Joy, queue_size=1)
    joy = Joy()
    
    while not rospy.is_shutdown():
        joy.axes[0] = joints[0]
        joy.axes[1] = joints[1]
        joy.axes[3] = joints[2]
        joy.axes[4] = joints[3]
        pub.publish(joy)
        rate.sleep()
