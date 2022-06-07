#!/usr/bin/env python3
#
# Visualization for skin sensor testing board
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
import matplotlib.gridspec as gridspec
import matplotlib.animation as animation
from mpl_toolkits.mplot3d import axes3d
from itertools import product, chain
#from mpl_toolkits.mplot3d import Axes3D
#from mpl_toolkits.mplot3d.art3d import Poly3DCollection

import skin

# List of devices to try (if not given on cmdline)
devices = ['/dev/ttyUSB0', '/dev/ttyUSB1']
baud_rate = 2000000  # default, overrideable at cmdline

# Physical arrangement of tactile sensors on the film by number
# placement = np.array([
#     [2, 1,  9, 10],
#     [4, 3, 11, 12],
#     [6, 5, 13, 14],
#     [8, 7, 15, 16],
# ]) - 1

# Rotate 90 because patches are mounted sideways on octocan
placement = np.rot90(np.array([
    [1, 2, 10,  9],
    [3, 4, 12, 11],
    [6, 5, 13, 14],
    [8, 7, 15, 16],
]) - 1)

pos_to_cell = { p: c for p, c in enumerate(placement.ravel()) }
cell_to_pos = { c: p for p, c in enumerate(placement.ravel()) }

position2d = np.mgrid[:placement.shape[0], :placement.shape[1]].reshape(2, -1).T

LINESTYLE = {
    'color': 'black',
    'linewidth': 0.5,
}

CIRCLE_RADIUS = 0.45
DOT_RADIUS = 0.07
DOT_COLOR = '#444444'

FOCUS_RADIUS = 0.5
FOCUS_RADIUS_MIN = 0.01
FOCUS_PROPS = {
    'edgecolor': 'cadetblue',
    'facecolor': None,
    'lw': 3,
    'alpha': 0.5,
}

shutdown = False
total_frames = 0

def parse_cmdline():
    parser = argparse.ArgumentParser()
    parser.add_argument('config', choices=['test', 'octocan'], default='octocan', nargs='?', help='use configuration');
    parser.add_argument('--verbose', '-v', action='store_true', help='print more information')

    ser = parser.add_argument_group('Device configuration options')
    ser.add_argument('--device', '-d', type=str, help='use serial port')
    ser.add_argument('--patches', '-p', type=int, default=1, help='number of sensor patches')
    ser.add_argument('--cells', '-c', type=int, default=16, help='number of cells per patch')
    ser.add_argument('--baud', '-b', type=int, default=baud_rate, help='use baud rate')
    ser.add_argument('--history', '-n', metavar='N', type=int, default=100, help='store N of the last values read')
    ser.add_argument('--alpha', '-a', type=float, default=0.2, help='set alpha (0..1] for exponential averaging fall off')
    ser.add_argument('--log', '-l', metavar='CSV', type=str, default=None, help='log data to CSV file')
    ser.add_argument('--debug', metavar='FILE', type=str, default=None, help='log debugging information to FILE')
    ser.add_argument('--nocalibrate', action='store_true', help='do not perform baseline calibration on startup')

    plot = parser.add_argument_group('Plotting and visualization options')
    plot.add_argument('--style', choices=['line', 'circle', 'bar', 'bar2', 'web', 'text', 'null'], default='line', help='select plotting style')
    plot.add_argument('--pair', type=int, nargs=2, default=[2, 6], help='pair of patches to plot for bar2 style')
    plot.add_argument('--patch', type=int, default=2, help='patch to show for bar style')
    plot.add_argument('--delay', type=float, default=30, help='delay between plot updates in milliseoncds')
    plot.add_argument('--threshold', metavar='VALUE', type=int, default=None, help='emphasis activity based on threshold value')
    plot.add_argument('--figsize', metavar=('WIDTH', 'HEIGHT'), type=float, nargs=2, default=None, help='set figure size in inches')
    #plot.add_argument('--dup', type=int, help='mimic multiple patches by duplicating first patch DUP times')
    plot.add_argument('--only', metavar='PATCH', type=int, default=None, help='plot only patch out of many')
    plot.add_argument('--zmin', type=float, help='set minimum z-axis for 3D plots', default=-4000)#, default=-75000)
    plot.add_argument('--zmax', type=float, help='set maximum z-axis for 3D plots', default=4000)#, default=75000)
    plot.add_argument('--zrange', '-z', metavar='Z', type=float, default=None, help='set z-axis range to [-Z, +Z]')
    plot.add_argument('--yauto', action='store_true', help='autoscale y-axis')
    #plot.add_argument('--calib', metavar='CSVFILE', help='calibration data from CSVFILE') #old temp calib method for demo
    plot.add_argument('--profile', metavar='CSVFILE', help='dynamic range calibration from CSVFILE')
    plot.add_argument('--cheap', metavar='CSVFILE', help='write cheap range data to file [text style only]')

    cmdline = parser.parse_args()
    if cmdline.config == 'octocan':
        cmdline.patches = 8
        cmdline.cells = 16
    elif cmdline.config == 'test':
        cmdline.patches = 1
        cmdline.cells = 16
    if cmdline.figsize is None:
        if cmdline.style == 'line':
            cmdline.figsize = (8, 8) if cmdline.only else (15, 9)
        elif cmdline.style == 'circle':
            cmdline.figsize = (8, 6)
        elif cmdline.style == 'text':
            cmdline.figsize = (12, 8)
        else:
            cmdline.figsize = (8, 8)
    if cmdline.zrange:
        cmdline.zmin = -abs(cmdline.zrange)
        cmdline.zmax = abs(cmdline.zrange)
    return cmdline

cmdline = parse_cmdline()

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
    sensor = skin.Skin(patches=cmdline.patches, cells=cmdline.cells, device=device, history=cmdline.history)
    sensor.set_alpha(cmdline.alpha)
    if cmdline.profile:
        sensor.read_profile(cmdline.profile)
    return sensor


def line_init(sensor, patch):
    '''
    Plots initial layout artists
    '''
    nrows, ncols = placement.shape
    fig, axs = plt.subplots(nrows, ncols, sharex=False, sharey=True)
    fig.set_figwidth(cmdline.figsize[0])
    fig.set_figheight(cmdline.figsize[1])

    margin = 0.05
    plt.subplots_adjust(left=margin, right=1-margin, bottom=margin, top=1-margin, wspace=0.01, hspace=0.01)
    lines = [None]*sensor.cells
    axes = [None]*sensor.cells
    pos = 1
    for row in placement:
        for index in row:
            axes[index] = plt.subplot(nrows, ncols, pos)
            plt.axis('off')
            lines[index] = plt.plot(sensor.get_history(patch, index), **LINESTYLE)[0]
            plt.ylim(cmdline.zmin, cmdline.zmax)
            pos += 1
            axes[index].text(0.1, 0.9, str(index), transform=axes[index].transAxes, fontsize=12)
    stats = None
    return fig, (lines, axes, stats)

def line_update(frame, sensor, patch, args):
    '''
    Updates the plot for each frame
    '''
    lines, axes, stats = args
    ymin, ymax = plt.ylim()
    for index, line in enumerate(lines):
        h = sensor.get_history(patch, index)
        line.set_ydata(h)
        plt.autoscale(axis='y')
    global total_frames
    total_frames += 1

def allline_init(sensor):
    '''
    Plots initial layout artists
    '''
    global calib
    margin = 0.05
    adjust = { 'wspace': 0.01, 'hspace': 0.01 }
    patch_rows = 2 if sensor.patches > 4 else 1
    patch_cols = sensor.patches//2 if sensor.patches > 4 else sensor.patches
    cell_rows, cell_cols = placement.shape
    # nrows = 2*placement.shape[0]
    # ncols = (sensor.patches//2)*placement.shape[1]
    fig = plt.figure(figsize=cmdline.figsize, constrained_layout=False)
    patch_grid = fig.add_gridspec(patch_rows, patch_cols, **adjust)

    plt.subplots_adjust(left=margin, right=1-margin, bottom=margin, top=1-margin, wspace=0.01, hspace=0.01)
    lines = {}
    axes = {}
    for patch_pos in range(patch_rows*patch_cols):
        patch = patch_pos + 1
        cell_grid = gridspec.GridSpecFromSubplotSpec(cell_rows, cell_cols, subplot_spec=patch_grid[patch_pos], **adjust)
        # ax = plt.Subplot(fig, patch_grid[patch_pos])
        # ax.axis('off')
        # ax.set_title(str(patch))
        for cell_pos, (cell_row, cell_col) in enumerate(product(range(cell_rows), range(cell_cols))):
            cell = pos_to_cell[cell_pos]
            #print('cell', cell, 'in pos', cell_pos)

            ax = fig.add_subplot(cell_grid[cell_pos])
            ax.set_xticks([])
            ax.axis('off')
            
            text = str(patch) + ',' + str(cell)
            h = sensor.get_history(patch, cell)
            disabled = False
            # if calib is not None:
            #     # Have calibration
            #     if calib.loc[cell, 'inactive']:
            #         disabled = True
            #         text += ' disabled'
            #         ax.text(0.1, 0.9, text, transform=ax.transAxes, fontsize=10)
            #         ax.set_yticks([])
            #         continue
            #     else:
            #         delta = calib.loc[pos_to_cell[cell_pos], 'delta'].astype(float)
            #         h = h.astype(float)/delta
            #         ax.set_ylim(-2, 2)

            ax.text(0.1, 0.9, text, transform=ax.transAxes, fontsize=10)
            axes[patch, cell] = ax
            lines[patch, cell] = ax.plot(h, **LINESTYLE)[0]
            # if cmdline.zmin is not None and cmdline.zmax is not None:
            #     ax.set_ylim(cmdline.zmin, cmdline.zmax)
            #ax.autoscale(enable=True, axis='y')
            fig.add_subplot(ax)

            # #matplotlib v3.3.3 version?
            # cell_grid = patch_grid[patch_row, patch_col].subgridspec(cell_rows, cell_cols, wspace=0.01, hspace=0.01)
            # axs = cell_grid.subplots()
            # patch = patch_row*patch_cols + patch_col + 1
            # for (cell_row, cell_col), ax in np.ndenumerate(axs):
            #     cell = pos_to_cell[cell_row*cell_cols + cell_col]
            #     #axes[patch, cell] = plt.subplot(nrows, ncols, sensor.cells*(patch - 1) + cell_to_pos[cell] + 1)
            #     axes[patch, cell] = ax
            #     ax.axis('off')
            #     lines[patch, cell] = ax.plot(sensor.get_history(patch, cell), **LINESTYLE)[0]
            #     ax.set_ylim(cmdline.zmin, cmdline.zmax)
            #     ax.text(0.1, 0.9, str(patch) + ',' + str(cell), transform=ax.transAxes, fontsize=10)

    for ax in fig.get_axes():
        for spine in ax.spines.values():
            spine.set_visible(False)
        if ax.is_first_row():
            ax.spines['top'].set_visible(True)
        if ax.is_last_row():
            ax.spines['bottom'].set_visible(True)
        if ax.is_first_col():
            ax.spines['left'].set_visible(True)
        if ax.is_last_col():
            ax.spines['right'].set_visible(True)
    stats = None
    return fig, (lines, axes, stats)

def allline_update(frame, sensor, args):
    '''
    Updates the plot for each frame
    '''
    lines, axes, stats = args
    for patch in range(1, sensor.patches + 1):
        for cell in range(sensor.cells):
            h = sensor.get_history(patch, cell)
            # if calib is not None:
            #     # Have calibration
            #     if calib.loc[cell, 'inactive']:
            #         continue
            #     h = h.astype(float)/calib.loc[cell, 'delta'].astype(float)
            # else:
            # Not using calibration, autoscale
            hmin, hmax = h.min(), h.max()
            if hmin == hmax:
                hmin, hmax = -10, 10
            axes[patch, cell].set_ylim(hmin, hmax)
            lines[patch, cell].set_ydata(h)
    global total_frames
    total_frames += 1

# standard_axes = np.array([[1, 0, 0], [0, 1, 0], [0, 0, 1]])

# def patch_rotate(patch, v):
#     angle = -(patch - 1)*np.pi/4
#     patch_axes = np.array([
#         [np.cos(angle), np.sin(angle), 0],
#         [-np.sin(angle), np.cos(angle), 0],
#         [0, 0, 1]])
#     rotation = np.inner(patch_axes, standard_axes)
#     return np.inner(v, rotation)

# bars = None
# bars_base = None
# bars_out = None
# bars_wide = None
# bars_tall = None

# def make_bars(sensor):
#     bars = []
#     for p in range(sensor.patches):
#         c1 = bars_base[p].T
#         reading = [sensor.get_expavg(p+1, c) for c in range(sensor.cells)]
#         c1 += np.array([(reading[i]/cmdline.zmax)*bars_out[0].T[i] for i in range(len(reading))])
#         #/cmdline.zmax)*(bars_out[p].T)
#         #c1b = np.add(c1, reading)
#         c2 = c1 + bars_wide[p].T
#         c3 = c1 + bars_wide[p].T + bars_tall[p].T
#         c4 = c1 + bars_tall[p].T
#         corners = np.transpose(np.array([c1, c2, c3, c4]), axes=[1, 0, 2])
#         poly = Poly3DCollection(corners)
#         poly.set_facecolor('#888888')
#         bars.append(poly)
#         #ax.add_collection(poly)
#     return bars

# def bar_init(sensor):
#     global bars, bars_base, bars_out, bars_wide, bars_tall
#     fig = plt.figure(figsize=cmdline.figsize)
#     ax = fig.add_subplot(111, projection='3d')
#     width = depth = 1
#     # xx, yy = np.meshgrid(*(np.arange(d) for d in placement.shape))
#     # barx = xx.ravel()
#     # bary = yy.ravel()

#     distance = 6  #distance from center
#     yy, nzz = np.meshgrid(*(np.arange(d) for d in placement.shape))
#     y = yy.ravel()
#     z = -nzz.ravel()
#     base = np.array([np.full_like(y, distance), y, z]).T - np.array([0, 2, 0])

#     out = np.array([[1, 0, 0]]*sensor.cells)
#     wide = np.array([[0, 1, 0]]*sensor.cells)
#     tall = np.array([[0, 0, 1]]*sensor.cells)

#     bars_base = np.array([patch_rotate(p, base).T for p in range(1, sensor.patches + 1)])
#     bars_out = np.array([patch_rotate(p, out).T for p in range(1, sensor.patches + 1)])
#     bars_wide = np.array([patch_rotate(p, wide).T for p in range(1, sensor.patches + 1)])
#     bars_tall = np.array([patch_rotate(p, tall).T for p in range(1, sensor.patches + 1)])

#     bars = make_bars(sensor)
#     for bar in bars:
#         ax.add_collection(bar)

#     # zero = np.zeros_like(barx)
#     # place = placement.ravel()

#     # bars = []
#     # for p in [0]:#range(sensor.patches):
#     #     #bar = ax.bar3d(barx, bary, zero, width, depth, zero, color='#888888', shade=True)
#     #     breakpoint()
#     #     bar = ax.bar3d(x=bars_base[p][0], y=bars_base[p][1], z=bars_base[p][2],
#     #                    dx=bars_out[p][0], dy=bars_out[p][1], dz=bars_out[p][2],
#     #                    color='#888888', shade=True)
#     #     bars.append(bar)
#     #return fig, (ax, barx, bary, place)
#     ax.set_xlim3d(-10, 10)
#     ax.set_ylim3d(-10, 10)
#     ax.set_zlim3d(-10, 10)
#     #ax.set_zlim3d(cmdline.zmin, cmdline.zmax)
#     plt.show()
#     return fig, ax

# def bar_update(frame, sensor, args):
#     global bars
#     # width = depth = 1
#     # ax, barx, bary, place = args
#     # zero = np.zeros_like(barx)
#     # current = [sensor.get_expavg(patch, cell) for cell in place]
#     # ax.clear()
#     # bars[0].remove()
#     # bars[0] = ax.bar3d(x=barx, y=bary, z=zero, dx=width, dy=depth, dz=current, color='#888888', shade=True)
#     # ax.set_zlim3d(cmdline.zmin, cmdline.zmax)
#     ax = args
#     ax.clear()
#     # for bar in bars:
#     #     bar.remove()
#     bars = make_bars(sensor)
#     for bar in bars:
#         ax.add_collection(bar)
#     global total_frames
#     total_frames += 1


def stats_updater(sensor, view, sleep=2):
    global shutdown, total_frames
    bytes_before = sensor.total_bytes
    records_before = sensor.total_records
    before = datetime.datetime.now()
    num_cells = sensor.patches*sensor.cells
    frames_before = total_frames
    while not shutdown:
        time.sleep(sleep)
        now = datetime.datetime.now()
        bytes_now = sensor.total_bytes
        records_now = sensor.total_records
        frames_now = total_frames

        time_delta = (now - before).total_seconds()
        bytes_rate = (bytes_now - bytes_before)/time_delta
        records_rate = (records_now - records_before)/time_delta
        total_Hz = records_rate/num_cells
        frame_rate = (frames_now - frames_before)/time_delta

        bytes_before = bytes_now
        records_before = records_now
        frames_before = frames_now
        before = now

        #print("reader: %.2f KB/s  %.0f cells/s  %.1f Hz   plotter: %.1f fps" % (bytes_rate/1024, records_rate, total_Hz, frame_rate))


circle_active = np.zeros_like(placement.flatten(), dtype=bool)
circle_value = np.zeros_like(placement.flatten(), dtype=float)
circle_pos = np.zeros_like(position2d, dtype=float)

def circle_init(sensor, patch):
    #global calib
    fig = plt.figure(figsize=cmdline.figsize)
    plt.subplots_adjust(left=0.02, right=0.98, top=0.98, bottom=0.02)
    # plt.xlim(-0.5, 3.5)
    # plt.ylim(-3.5, 0.5)
    plt.tick_params(axis='both', which='both', bottom=False, top=False, left=False, right=False, labelbottom=False, labelleft=False)

    if cmdline.profile:
        profile = pd.read_csv(cmdline.profile).set_index(['patch', 'cell'])

    ax = plt.gca()
    ax.set_facecolor('white')
    cell_rows, cell_cols = placement.shape

    width = cell_cols - 1
    height = cell_rows - 1
    margin = 0.5
    
    plt.xlim(-width/2 - margin, width/2 + margin)
    plt.ylim(-height/2 - margin, height/2 + margin)

    circles = {}
    pos = {}
    for cell_pos, (row, col) in enumerate(product(range(cell_rows), range(cell_cols))):
        cell = pos_to_cell[cell_pos]
        pos[cell] = (-width/2 + col, height/2 - row)
        circle_pos[cell] = pos[cell]
        enabled = profile.loc[patch, cell].c1 != 0 if cmdline.profile else True
        circle_active[cell] = enabled

        circles[cell] = plt.Circle(pos[cell], CIRCLE_RADIUS, color='white', zorder=10)
        ax.add_patch(circles[cell])

        props = { 'facecolor': DOT_COLOR if enabled else 'w', 'edgecolor': 'w' if enabled else DOT_COLOR }
        ax.add_patch(plt.Circle(pos[cell], DOT_RADIUS, zorder=20, **props))
        plt.text(pos[cell][0], pos[cell][1], str(cell), fontsize=8, ha='center', va='center', color='w' if enabled else DOT_COLOR, zorder=30)

    # if cmdline.profile is not None:
    #     vmin, vmax = -4000, 4000
    # else:
    vmin, vmax = cmdline.zmin, cmdline.zmax
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
    ax.set_aspect('equal')
    for spine in ax.spines:
        ax.spines[spine].set_visible(False)

    pos_center = np.array(list(pos.values())).mean(axis=0)
    active_pos = circle_pos[circle_active]
    return fig, (circles, mapper, patch, pos, ax, pos_center, 0, active_pos)

circle_focus = None
circle_max = np.full_like(placement.flatten(), cmdline.zmax, dtype=float)
focus_alpha = 0.5

deadspace = 0

def circle_update(frame, sensor, args):
    '''
    Updates the plot for each frame
    '''
    global circle_max, circle_focus
    circles, mapper, patch, pos, ax, last_pos, last_magnitude, active_pos = args
    for cell in range(sensor.cells):
        avg = abs(sensor.get_expavg(patch, cell))
        clr = mapper.to_rgba(avg)
        circles[cell].set_color(clr)

        # mx = circle_max[cell] = max(avg, circle_max[cell])
        # threshold = deadspace*mx
        # if avg < threshold:
        #     v = 0
        # else:
        #     v = (avg - threshold)/(mx - threshold)
        # circle_value[cell] = min(v, circle_max[cell])  #clip to max

        circle_value[cell] = min(avg, circle_max[cell])  #clip to max

    # P = circle_value[circle_active]
    # P = P**2
    # focus_magnitude = P.sum()
    # if focus_magnitude == 0:
    #     focus_pos = circle_pos.mean(axis=0)
    # else:
    #     focus_pos = (P/focus_magnitude).dot(active_pos)
    # focus_pos = focus_alpha*focus_pos + (1 - focus_alpha)*last_pos

    # focus_magnitude = focus_alpha*focus_magnitude + (1 - focus_alpha)*last_magnitude

    focus_magnitude = ((circle_value/circle_max)[circle_active]).sum()
    field = ((circle_value/circle_max)[circle_active]/focus_magnitude)
    focus_pos = field.dot(circle_pos[circle_active])
    focus_pos = focus_alpha*focus_pos + (1 - focus_alpha)*last_pos

    focus_magnitude = focus_alpha*focus_magnitude + (1 - focus_alpha)*last_magnitude
    
    print(focus_pos, focus_magnitude)

    if circle_focus:
        circle_focus.remove()
    focus_pos *= 2*np.sqrt(2)
    circle_focus = plt.Circle(focus_pos, max(FOCUS_RADIUS_MIN, FOCUS_RADIUS*focus_magnitude), zorder=100, **FOCUS_PROPS)
    ax.add_patch(circle_focus)

    global total_frames
    total_frames += 1

position2d_x, position2d_y = position2d.T[0], position2d.T[1]
Zero = np.zeros(len(position2d), dtype=int)

bar2_height_max = 1000 if cmdline.profile else 10000#1e7
bar2_separation = 2.5*bar2_height_max
top_z = np.full(len(position2d), bar2_separation)

def bar_init(sensor):
    global bar_bars
    patch = cmdline.patch
    fig = plt.figure(figsize=cmdline.figsize)
    ax = fig.add_subplot(111, projection='3d')
    bar_bars = ax.bar3d(position2d_x, position2d_y, Zero, 1, 1, Zero, color='C0')
    ax.set_zlim(0, bar2_height_max)
    return fig, (patch, ax)


def bar_update(frame, sensor, args):
    global bar_bars
    patch, ax = args
    #height = [max(0, sensor.get_expavg(patch, cell)) for cell in range(sensor.cells)]

    #height = np.clip(sensor.get_expavg_all(patch), 0, bar2_height_max)
    height = np.zeros((sensor.cells,), dtype=int)
    for cell in [4, 2]:
        height[cell_to_pos[cell]] = np.clip(sensor.get_expavg(patch, cell), 0, bar2_height_max)

    bar_bars.remove()
    bar_bars = ax.bar3d(position2d_x, position2d_y, Zero, 1, 1, height, color='beige')

    global total_frames
    total_frames += 1

bar2_bars = []


def bar2_mkbars(ax, sensor):
    patch_top, patch_bottom = cmdline.pair

    # height_top = [max(0, sensor.get_expavg(patch_top, cell)) for cell in range(sensor.cells)]
    # height_bottom = [max(0, sensor.get_expavg(patch_bottom, cell)) for cell in range(sensor.cells)]

    # height_top = sensor.get_expavg_all(patch_top)
    # height_bottom = sensor.get_expavg_all(patch_bottom)

    height_top = -np.clip(sensor.get_expavg_all(patch_top), 0, bar2_height_max)
    height_bottom = np.clip(sensor.get_expavg_all(patch_bottom), 0, bar2_height_max)

    bars_top = ax.bar3d(position2d_x, position2d_y, top_z, 1, 1, height_top, color='lightsteelblue')
    bars_bottom = ax.bar3d(position2d_x, position2d_y, Zero, 1, 1, height_bottom, color='beige')
    return [bars_top, bars_bottom]


def bar2_init(sensor):
    global bar2_bars
    fig = plt.figure(figsize=cmdline.figsize)
    ax = fig.add_subplot(111, projection='3d')
    bars2_bars = bar2_mkbars(ax, sensor)
    ax.set_zlim(0, bar2_separation)
    return fig, ax


def bar2_update(frame, sensor, args):
    global bar2_bars
    ax = args

    # for bar in bar2_bars:
    #     bar.remove()
    ax.clear()
    bars2_bars = bar2_mkbars(ax, sensor)

    global total_frames
    total_frames += 1


def web_init(sensor):
    fig = plt.figure(figsize=cmdline.figsize)
    plt.axes(projection='polar')
    theta = np.zeros((sensor.patches + 1,))
    theta[:sensor.patches] = np.arange(0, 2*np.pi, 2*np.pi/sensor.patches)
    lines = plt.plot(theta, np.zeros((sensor.patches + 1,)), 'o-')[0]
    if cmdline.profile:
        #plt.ylim(0, 4000)
        plt.ylim(0, 1000)
    else:
        plt.ylim(0, 1e7)
    return fig, lines


def web_update(frame, sensor, args):
    lines = args
    values = np.zeros((sensor.cells,))
    patch_avg = []
    for patch in range(1, sensor.patches + 1):
        for cell in range(sensor.cells):
            values[cell] = sensor.get_expavg(patch, cell)
        patch_avg.append(values.mean())

    # Complete a loop by appending first value
    patch_avg.append(patch_avg[0])

    lines.set_ydata(patch_avg)

    if frame % 20 == 0:
        for patch in range(sensor.patches):
            print(' %10d' % patch_avg[patch], end='')
        print()

    global total_frames
    total_frames += 1

def text_mkstr(df, sensor, patch, cell):
    addr = (patch, cell)
    history = sensor.get_history(patch, cell)
    #current = sensor.get_expavg(patch, cell)
    vmax = max(df.loc[addr, 'max'], history.max())
    vmin = min(df.loc[addr, 'min'], history.min())
    df.loc[addr, 'max'] = vmax
    df.loc[addr, 'min'] = vmin
    df.loc[addr, 'batch'] += int(history.mean())
    df.loc[addr, 'count'] += 1
    return '%d\n%d\n%d' % (vmax, history[-1], vmin)

def text_init(sensor):
    if cmdline.config != 'octocan':
        print('"text" style is for octocan only', file=sys.stderr)
        sys.exit(1)

    time.sleep(2)

    index_cols = ['patch', 'cell']
    data_cols=['min', 'max', 'batch', 'count']
    rows = []
    for patch in range(1, cmdline.patches + 1):
        for cell in range(cmdline.cells):
            current = int(sensor.get_expavg(patch, cell))
            rows.append([patch, cell, current, current, 0, 0])
    df = pd.DataFrame(rows, columns=index_cols + data_cols, dtype=int).set_index(index_cols)
    text = {}

    def make_patch(ax, patch):
        # Rows and columns of cells for this patch
        #global df, text
        nrows, ncols = placement.shape
        for row, col in np.ndindex(placement.shape):
            cell = placement[row, col]
            textstr = text_mkstr(df, sensor, patch, cell)
            text[patch, cell] = ax.text(col + 1, row, textstr, ha='right', va='bottom', fontsize=12)
        ax.set_title('Patch %d' % patch)
        ax.set_ylim(0, ncols)
        ax.set_xlim(0, nrows)
        ax.axis('off')

    # Rows and columns of patches
    nrows = 2
    ncols = 4
    fig, axs = plt.subplots(nrows, ncols, figsize=cmdline.figsize)
    for patch in range(cmdline.patches):
        make_patch(axs[patch//ncols, patch % ncols], patch + 1)  # patch IDs start at 1
    plt.subplots_adjust(left=0.05, right=0.95, top=0.95, bottom=0.05, wspace=0.05)
    return fig, (df, text)

def text_update(frame, sensor, args):
    df, text = args
    for patch in range(1, cmdline.patches + 1):
        for row, col in np.ndindex(placement.shape):
            cell = placement[row, col]
            text[patch, cell].set_text(text_mkstr(df, sensor, patch, cell))
    global total_frames
    total_frames += 1

def null_init(sensor):
    return plt.figure(figsize=cmdline.figsize), None

threshold = 10
def null_update(frame, sensor, args):
    values = np.zeros((sensor.cells,))
    for patch in range(1, sensor.patches + 1): # patch IDs start at 1
        for cell in range(sensor.cells):
            values[cell] = sensor.get_expavg(patch, cell)
        m = values[values != 0].mean()
        print(' %11.0f %s' % (m, 'O' if m > threshold else '.'), end='')
    print()
    global total_frames
    total_frames += 1

def calibrate(sensor, keep=True, show=True):
    sensor.calibrate_start()
    print('Baseline calibration... DO NOT TOUCH!')
    time.sleep(4)
    sensor.calibrate_stop()
    print('Baseline calibration finished')
    baseline = None
    if keep:
        rows = []
        for patch in range(1, sensor.patches + 1):
            for cell in range(sensor.cells):
                rows.append([patch, cell, sensor.get_calib(patch, cell)])
        baseline = pd.DataFrame(rows, columns=['patch', 'cell', 'calibration'])
        if show:
            print(baseline)
    return baseline
            
def main():
    global shutdown
    #sensor = skin.Skin(patches=cmdline.patches, cells=cmdline.cells, device=cmdline.device, history=cmdline.history)
    sensor = setup_octocan()
    sensor.set_alpha(cmdline.alpha)
    if cmdline.log:
        sensor.log(cmdline.log)
    if cmdline.debug:
        sensor.debuglog(cmdline.debug)
    sensor.start()

    if not cmdline.nocalibrate:
        calibrate(sensor)

    # if cmdline.profile:
    #     sensor.read_profile(cmdline.profile)

    stats_thread = threading.Thread(target=stats_updater, args=(sensor, None))
    stats_thread.start()

    styles = {
        'line': (allline_init, allline_update),
        'circle': (circle_init, circle_update),
        'bar': (bar_init, bar_update),
        'bar2': (bar2_init, bar2_update),
        'web': (web_init, web_update),
        'text': (text_init, text_update),
        'null': (null_init, null_update),
    }

    if cmdline.style in styles:
        if cmdline.only and cmdline.style == 'line':
            fig, args = styles[cmdline.style][0](sensor, cmdline.only)
            anim = animation.FuncAnimation(fig, func=styles[cmdline.style][1], fargs=(sensor, args), interval=cmdline.delay)
        else:
            if cmdline.only:
                fig, args = styles[cmdline.style][0](sensor, patch=cmdline.only)
            else:
                fig, args = styles[cmdline.style][0](sensor)
            anim = animation.FuncAnimation(fig, func=styles[cmdline.style][1], fargs=(sensor, args), interval=cmdline.delay)
    else:
        print('Unknown style:', cmdline.style, file=sys.stderr)
        sys.exit(1)

    # if cmdline.style == 'line':
    #     # if cmdline.only:
    #     #     patch = cmdline.only
    #     #     fig, args = line_init(sensor, patch)
    #     #     anim = animation.FuncAnimation(fig, func=line_update, fargs=(sensor, patch, args), interval=cmdline.delay)
    #     # else:
    #     fig, args = allline_init(sensor)
    #     anim = animation.FuncAnimation(fig, func=allline_update, fargs=(sensor, args), interval=cmdline.delay)
    # # elif cmdline.style == 'bar':
    # #     #fig, args = bar_init(sensor, patch)
    # #     fig, args = bar_init(sensor)
    # #     bar_update(0, sensor, args)
    # #     #anim = animation.FuncAnimation(fig, func=bar_update, fargs=(sensor, args), interval=cmdline.delay);
    # elif cmdline.style == 'circle':
    #     if sensor.patches > 1:
    #         print("Warning: Multiple patches given, using only first patch")
    #     fig, args = circle_init(sensor, 1)
    #     anim = animation.FuncAnimation(fig, func=circle_update, fargs=(sensor, args), interval=cmdline.delay)
    # elif cmdline.style == 'bar':
    #     fig, args = bar_init(sensor, 1)
    #     anim = animation.FuncAnimation(fig, func=bar_update, fargs=(sensor, args), interval=cmdline.delay)
    # # elif cmdline.style == 'avgbar':
    # #     fig, args = avgbar_init(sensor, 1)
    # #     anim = animation.FuncAnimation(fig, func=avgbar_update, fargs=(sensor, args), interval=cmdline.delay)
    # elif cmdline.style == 'web':
    #     fig, args = web_init(sensor)
    #     anim = animation.FuncAnimation(fig, func=web_update, fargs=(sensor, args), interval=cmdline.delay)

    def calibrate_button(sensor):
        calibrate(sensor)
        if cmdline.style == 'text':
            df, _ = args
            for patch in range(1, cmdline.patches + 1):
                for cell in range(cmdline.cells):
                    current = int(sensor.get_expavg(patch, cell))
                    df['min'] = df['max'] = current
                    df['batch'] = df['count'] = 0

    plt.figure(figsize=(1,1))
    ax = plt.axes()
    button = mpl.widgets.Button(ax, 'Tare')
    button.label.set_fontsize(24)
    button.on_clicked(lambda _: calibrate_button(sensor))

    plt.show()

    shutdown = True
    stats_thread.join()

    if cmdline.style == 'text' and cmdline.cheap:
        df, _ = args
        print("Writing", cmdline.cheap)
        df.to_csv(cmdline.cheap, index=True)

if __name__ == '__main__':
    main()
#EOF
