#!/usr/bin/env python3
#
# Visualization for skin sensor testing board
#
# Bryan Harris
# bryan.harris.1@louisville.edu

import sys
import struct
import serial
import datetime
import argparse
import threading
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# Physical arrangement of tactile sensors on the film by number
placement = np.array([
    [2, 1,  9, 10],
    [4, 3, 11, 12],
    [6, 5, 13, 14],
    [8, 7, 15, 16],
])

# Start and stop codes sent to controller
START_CODE = struct.pack('B', ord('1'))
STOP_CODE  = struct.pack('B', ord('0'))

# Format of data frame read from controller
FRAME_FORMAT = '>' + ('i'*placement.size)

LINESTYLE = {
    'color': 'black',
    'linewidth': 2,
}

class Ring(object):
    '''
    Ring buffer that is always full and automatically overwrites
    itself with appends.
    '''
    def __init__(self, capacity, dtype=int):
        self.buf = np.zeros(capacity, dtype=dtype)
        self.pos = 0
        # self.min = 0
        # self.max = 0

    def __getitem__(self, index):
        self.buf[(self.pos + index) % self.buf.size]

    def __len__(self):
        return self.buf.size

    def append(self, item):
        self.buf[self.pos] = item
        self.pos = (self.pos + 1) % self.buf.size
        # self.min = min(self.min, item)
        # self.max = max(self.max, item)

    def aligned(self):
        'Returns an array aligned with pos as first element'
        return np.roll(self.buf, -self.pos)

    # def __repr__(self):
    #     return self.__class__.__name__ + '(' + str(self.buf) + ', pos=' + str(self.pos) + ')'

def parse_cmdline():
    parser = argparse.ArgumentParser()
    parser.add_argument('--verbose', '-v', action='store_true', help='print more information')

    ser = parser.add_argument_group('Serial connection options')
    ser.add_argument('--port', '-p', type=str, default='/dev/ttyUSB0', help='use serial port')
    ser.add_argument('--baud', '-b', type=int, default=909091, help='use baud rate')
    ser.add_argument('--timeout', type=float, default=3.0, help='set timeout (in seconds) for serial reads')

    plot = parser.add_argument_group('Plotting and visualization options')
    plot.add_argument('--history', '-n', metavar='N', type=int, default=200, help='show N of the last values read')
    plot.add_argument('--delay', type=float, default=175, help='delay between plot updates in milliseoncds')
    plot.add_argument('--noroll', action='store_true', help='update plot without rolling newest value to the right')
    plot.add_argument('--threshold', metavar='VALUE', type=int, default=None, help='emphasis activity based on threshold value')
    plot.add_argument('--avgframes', metavar='N', default=None, help='average performance stats across N frames')
    plot.add_argument('--figsize', metavar=('WIDTH', 'HEIGHT'), type=float, nargs=2, default=(8, 8), help='set figure size in inches')
    cmdline = parser.parse_args()
    
    # Update stats once every two seconds by default
    if cmdline.avgframes is None:
        cmdline.avgframes = int(2000.0/cmdline.delay)
    return cmdline

def read_frames(conn, rings):
    frame_reader = struct.Struct(FRAME_FORMAT)

    # Request restart from controller
    conn.write(STOP_CODE)
    conn.write(START_CODE)

    global bytes_read
    bytes_read = 0

    # Read data frames
    print('Reading from', conn.port, file=sys.stderr)
    global shutdown
    while not shutdown:
        frame = conn.read(frame_reader.size)
        if len(frame) < frame_reader.size:
            print('Read error', file=sys.stderr)
            break
        bytes_read += frame_reader.size
        values = frame_reader.unpack_from(frame)
        for index, value in enumerate(values):
            rings[index].append(value)

    # Inform controller that we're finished
    print('Stopping', file=sys.stderr)
    conn.write(STOP_CODE)

def SI_bytes(x):
    if x == 0:
        return '0 '
    SI = {0: '', 10: 'K', 20: 'M', 30: 'G', 40: 'T', 50: 'P', 60: 'E', 70: 'Z', 80: 'Y'}
    power = 10*int(np.log2(np.abs(x))/10.0)
    prefix = SI.get(power)
    value = x*2**(-power)
    num_digits = np.ceil(np.log10(np.abs(value)))
    fmt = '%.0f %s' if num_digits >= 2 or value == round(value, 0) else '%.1f %s'
    return fmt % (value, prefix) if prefix else '%.0f' % x

last_read = 0
last_time = datetime.datetime.now()
since_update = 0
def update_readrate(stats):
    '''
    Updates the read rate string, less often than each frame
    '''
    global bytes_read
    global last_read
    global last_time
    global since_update
    if since_update < cmdline.avgframes:
        since_update += 1
    else:
        #time = since_update*(0.001*cmdline.delay)
        now = datetime.datetime.now()
        time_since = (now - last_time).total_seconds()
        bytes_since = bytes_read - last_read
        read_rate = bytes_since/time_since
        if cmdline.verbose:
            print(SI_bytes(read_rate) + 'B/s, ' + SI_bytes(bytes_since) + 'B in ' + str(time_since) + 's')

        last_read = bytes_read
        last_time = now
        since_update = 0
        stats.set_text('Reading ' + SI_bytes(read_rate) + 'B/s')

def plot_update(frame, rings, lines, axes, stats):
    '''
    Updates the plot for each frame
    '''
    update_readrate(stats)
    for index, line in enumerate(lines):
        data = rings[index].buf if cmdline.noroll else rings[index].aligned()
        line.set_ydata(data)
        low = data.min() - 1
        high = data.max() + 1
        axes[index].set_ylim(low, high)
        if cmdline.threshold is not None:
            height = high - low
            if height > cmdline.threshold:
                line.set_linewidth(2)
                line.set_color('k')
            else:
                gray = 1 - 0.75*(height)/cmdline.threshold
                line.set_linewidth(1)
                line.set_color((gray,)*3)
        #axes[index].set_ylim(rings[index].min - 1, rings[index].max + 1)

def plot_init(rings):
    '''
    Plots initial layout artists
    '''
    nrows, ncols = placement.shape
    fig, axs = plt.subplots(nrows, ncols, sharex=True)
    fig.set_figwidth(cmdline.figsize[0])
    fig.set_figheight(cmdline.figsize[1])

    margin = 0.05
    plt.subplots_adjust(left=margin, right=1-margin, bottom=margin, top=1-margin, wspace=0.01, hspace=0.01)
    lines = [None]*len(rings)
    axes = [None]*len(rings)
    pos = 1
    for row in placement:
        for index in row:
            axes[index - 1] = plt.subplot(nrows, ncols, pos)
            plt.axis('off')
            lines[index - 1] = plt.plot(rings[index - 1].buf, **LINESTYLE)[0]
            pos += 1
    stats = plt.text(1 - margin, 0.5*margin, '',
                     ha='right', va='center', transform=fig.transFigure)
    return fig, lines, axes, stats

def main():
    global shutdown
    shutdown = False

    conn = serial.Serial(cmdline.port, cmdline.baud, timeout=cmdline.timeout)
    rings = [Ring(cmdline.history) for i in range(placement.size)]
    read_thread = threading.Thread(target=read_frames, args=(conn, rings))
    fig, lines, axes, stats = plot_init(rings)
    read_thread.start()
    anim = animation.FuncAnimation(fig, func=plot_update, fargs=(rings, lines, axes, stats), interval=cmdline.delay)
    plt.show()
    shutdown = True
    read_thread.join()

if __name__ == '__main__':
    global cmdline
    cmdline = parse_cmdline()
    main()
#EOF
