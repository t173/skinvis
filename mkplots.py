#!/usr/bin/env python3

import sys

from sync import *

def save_as(suffix):
    if cmdline.output:
        filename = "%s-%s.%s" % (cmdline.output, suffix, cmdline.fmt)
        status("Saving " + filename)
        plt.savefig(filename, fmt=cmdline.fmt, bbox_inches='tight')

if cmdline.output is None:
    sys.exit()

plot_presses(force)
save_as("presses")

for cell in range(16):
    plot_cell(cell)
    save_as("cell%d" % cell)

df = calibrate(force, sensor)
if cmdline.output:
    filename = cmdline.output + "-calib.csv"
    status("Saving " + filename)
    df.to_csv(filename)
