#!/usr/bin/env python3

import sys
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation

from sklearn import metrics
from sklearn.linear_model import LinearRegression

force_label = "Indentation force (N)"

other_style = { 'ls': '-', 'lw': 0.5, 'c': '0.7', 'label': 'other' }
down_style = { 'ls': '-', 'lw': 3, 'c': 'r', 'label': 'press' }
up_style = { 'ls': '-', 'lw': 3, 'c': 'b', 'label': 'release' }

def parse_cmdline():
    parser = argparse.ArgumentParser()
    parser.add_argument('force', help='input force data')
    parser.add_argument('sensor', help='output sensor data')
    parser.add_argument('--threshold', '-t', type=float, default=None, help='threshold for press detection')
    #parser.add_argument('--alpha', '-a', type=float, default=0.1, help='alpha value for exponential averaging')
    return parser.parse_args()

def SI_bytes(x, base=2, space=False):
    if x == 0:
        return '0 ' if space else '0'
    SI = {0: '', 10: 'K', 20: 'M', 30: 'G', 40: 'T', 50: 'P', 60: 'E', 70: 'Z', 80: 'Y'}
    if base == 2:
        power = 10*int(np.log2(np.abs(x))/10.0)
        prefix = SI.get(power)
        value = x*2**-power
    elif base == 10:
        power = 3*int(np.log10(np.abs(x))/3.0)
        prefix = SI.get(10*power//3)
        value = x*10**-power
    else:
        prefix = None
    num_digits = np.ceil(np.log10(np.abs(value)))
    fmt = '%.0f%s%s' if num_digits >= 2 or value == round(value, 0) else '%.1f%s%s'
    return fmt % (value, ' ' if space else '', prefix) if prefix else '%.0f' % x

def status(*args):
    print(*args, file=sys.stderr)

def check_timeline(df, threshold=np.timedelta64(1, 's')):
    steps = pd.Series(df.index).diff()
    max_steps = max(steps.dropna())
    status("\ttime steps: min", min(steps.dropna()), "max", max_steps)
    if max_steps > threshold:
        status("Warning: possible discontinuity at", df.index[steps.argmax()])
        breakpoint()
        return False
    return True

def read_sensor(filename):
    print("Reading sensor data:", filename, file=sys.stderr)
    df = pd.read_csv(filename, index_col='time', parse_dates=['time'],
                     date_parser=lambda x: pd.to_datetime(x, unit='s', origin='unix'))
    df = df.fillna(0).astype(int)
    addr = df.columns.str.extract(r'patch(\d+)_cell(\d+)').astype(int)
    df.columns = addr.itertuples(index=False, name=None)
    return df


def read_force(filename):
    print("Reading force data:", filename, file=sys.stderr)
    df = pd.read_csv(filename, sep='\t ', engine='python', names=['time', 'force'])
    if df.iloc[0]['force'] == 0:
        df.drop(0, inplace=True)
    time = pd.to_datetime(df['time'], format='%H:%M:%S.%f_%Y/%m/%d')
    return df[['force']].set_index(time)


def resample(df, freq='ms'):
    """
    Resamples and interpolates data to a fixed frequency
    """
    types = { df.columns[i]: df.dtypes[i] for i in range(len(df.columns)) }
    return df.resample(freq).mean().interpolate().astype(types)
    

def smooth(df, size=1000):
    return df.rolling(window=size).mean().dropna()


def search(cmp_fn, low, high, iterations=20, args=[]):
    global path
    path = []
    for i in range(iterations):
        x = (low + high)/2
        comparison = cmp_fn(x, *args)
        path.append((x, low, high, comparison))
        if comparison == 0:
            break
        elif comparison < 0: # too low
            low = x
        else:  # too high
            high = x
    return x

def plot_path(df):
    p = pd.DataFrame(path, columns=['x', 'low', 'high', 'cmp'])
    p.index = p.index*len(df)/len(p)
    plt.plot(np.arange(len(df)), df.force, '-', lw=1, c='b')
    plt.plot(p.low, '-', lw=1, c='k')
    plt.plot(p.high, '-', lw=1, c='k')
    plt.plot(p.x, '-', lw=2, c='r')
    ax = plt.gca()
    ax.spines['top'].set_visible(False)
    ax.spines['bottom'].set_visible(False)
    ax.spines['right'].set_visible(False)
    ax.xaxis.set_visible(False)

def cut_threshold(df, threshold, step, field='force'):
    above = df[df[field].notna() & (df[field] >= threshold)]
    presses = (pd.Series(above.index).diff() > step).cumsum()
    presses.name = 'press'
    return pd.DataFrame(presses).set_index(above.index)

def detect_presses(df, threshold=None, expected=16):
    """
    Detect presses based on rising/falling edges of force data F based
    on a threshold value found using binary search
    """
    status("Detecting presses")
    if type(df) != pd.core.frame.DataFrame:
        raise ValueError("Expected DataFrame")
    steps = pd.Series(df.index).diff().dropna()
    if steps.nunique() != 1:
        raise ValueError("DataFrame not continuously indexed (did you resample?)")
    step = steps.unique()[0]

    def cmp_cut(x):
        cuts = cut_threshold(df, x, step)
        return 1 if cuts['press'].nunique() == expected else -1

    if threshold is None:
        #status("Searching for threshold value")
        #threshold = search(cmp_cut, df.force.min(), df.force.max())
        threshold = None
        for x in np.arange(df.force.min().round(-3), (df.force.min() + df.force.max())/2, 0.001):
            if cut_threshold(force, x, step).nunique()[0] == expected:
                threshold = x
                break
        if threshold is None:
            status("Could not find threshold value")
            breakpoint()
        status("Found threshold value", threshold)
    presses = cut_threshold(df, threshold, step)

    if presses['press'].nunique() != expected:
        status("Warning: Expected", expected, "presses, but found", presses.nunique(), "(check threshold value)")
    return presses


def plot_presses(df):
    plt.xticks(rotation=-30, ha='left', va='top')
    plt.plot(df.force, '-', lw=1, c='0.5', zorder=1)
    plt.xlabel("Time", fontsize=14)
    plt.ylabel(force_label, fontsize=14)
    plt.ylim(ymin=0)
    ax = plt.gca()
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)
    for press in df.press.dropna().unique():
        plt.plot(df[df.press == press].force, '-', lw=3, zorder=10, label=int(press))

def get_press_data(force, sensor, cell, patch=1):
    press = cell_to_press[cell]
    f = force[force.press == press]
    other = ~force.index.isin(f.index)
    peak = f.force.argmax()
    f_down = f.iloc[:peak]
    f_up = f.iloc[peak:]
    s_down = sensor[sensor.index.isin(f_down.index)]
    s_up = sensor[sensor.index.isin(f_up.index)]

    adj_mask = np.zeros((len(force),), dtype=bool)
    for adjacent_cell in adjacent_to_cell[cell]:
        adj_mask |= force.press == cell_to_press[adjacent_cell]
    f_adj = force[adj_mask]
    s_adj = sensor[sensor.index.isin(f_adj.index)]
    other &= ~adj_mask
    return f_down, f_up, s_down, s_up, f_adj, s_adj, other

def plot_cell_vs(force, sensor, cell, patch=1):
    args = get_press_data(force, sensor, cell, patch)
    f_down, f_up, s_down, s_up, f_adj, s_adj, other = args
    f_notpressed = force[other]
    s_notpressed = sensor[sensor.index.isin(f_notpressed.index)]

    ax = plt.gca()
    ax.spines['top'].set_visible(False)
    ax.spines['bottom'].set_visible(False)
    ax.spines['left'].set_visible(False)
    ax.spines['right'].set_visible(False)
    plt.xlabel(force_label, fontsize=12)
    plt.ylabel("Sensor value", fontsize=12)
    plt.xlim(min(0, force.force.min()), force.force.max())

    plt.plot(f_down.force, s_down[patch, cell], zorder=11, **down_style)
    plt.plot(f_up.force, s_up[patch, cell], zorder=10, **up_style)
    plt.plot(f_adj['force'], s_adj[patch, cell],
             '-', lw=1, c='0.2', zorder=5,
             label='adjacent cell pressed (' + ', '.join(str(c) for c in sorted(adjacent_to_cell[cell])) + ')',)
    plt.plot(f_notpressed.force, s_notpressed[patch, cell], zorder=1, **other_style)
    #plt.legend(loc='upper center', ncol=2, frameon=False, bbox_to_anchor=(0.5, -0.2))
    handles, labels = plt.gca().get_legend_handles_labels()
    return args, handles, labels

def plot_cell_press(force, sensor, cell, patch=1, args=None):
    ax = plt.gca()
    ax.spines['top'].set_visible(False)
    ax.spines['bottom'].set_visible(False)
    ax.spines['left'].set_visible(False)
    ax.spines['right'].set_visible(False)
    plt.xlabel("Time from press", fontsize=12)
    #plt.ylabel("Sensor value", fontsize=12)

    args_, handles, labels = args
    if args_ is None:
        args_ = get_press_data(force, sensor, cell, patch)
    f_down, f_up, s_down, s_up, f_adj, s_adj, other = args_

    #margin = np.timedelta64(3, 's')

    # Previous press
    prev_press_num = cell_to_press[cell] - 1
    if prev_press_num < 0: # no previous press
        end_of_prev = force.index.min()
        start_of_prev = force.index.min()
    else:
        end_of_prev = force[force.press == prev_press_num].index.max()
        start_of_prev = force[force.press == prev_press_num].index.min()

    # Next press
    next_press_num = cell_to_press[cell] + 1
    if next_press_num > force.press.max():  # no next press
        start_of_next = force.index.max()
        end_of_next = force.index.max()
    else:
        start_of_next = force[force.press == next_press_num].index.min()
        end_of_next = force[force.press == next_press_num].index.max()

    before = (sensor.index > end_of_prev) & (sensor.index <= s_down.index.min())
    after = (sensor.index < start_of_next) & (sensor.index >= s_up.index.max())
    start_of_press = s_down.index.min()

    s_down.index -= start_of_press
    s_up.index -= start_of_press
    sensor_copy = sensor.copy()
    sensor_copy.index -= start_of_press

    plt.plot(s_down[patch, cell], zorder=10, **down_style)
    plt.plot(s_up[patch, cell], zorder=10, **up_style)

    plt.plot(sensor_copy[before][patch, cell], zorder=1, **other_style)
    plt.plot(sensor_copy[after][patch, cell], zorder=1, **other_style)

    plt.figlegend(handles=handles, labels=labels, loc='lower center', ncol=4, frameon=False)
    return args_

def plot_cell(cell, patch=1):
    global force, sensor
    fig = plt.figure(figsize=(9, 3))
    left = plt.subplot(1, 2, 1)
    args = plot_cell_vs(force, sensor, cell, patch)
    right = plt.subplot(1, 2, 2, sharey=left)
    plot_cell_press(force, sensor, cell, patch, args=args)
    
    plt.suptitle("Cell %d" % cell, fontsize=14)
    plt.subplots_adjust(left=0.13, right=0.95, top=0.90, bottom=0.3, wspace=0.3)

cmdline = parse_cmdline()
force_orig = read_force(cmdline.force)
sensor_orig = read_sensor(cmdline.sensor)

# Check for differences in time zone
time_diff = sensor_orig.index.min() - force_orig.index.min()
tz_diff = time_diff.round('H')
if time_diff != np.timedelta64(0, 'h'):
    status("Detected time difference of", abs(time_diff))
    if tz_diff == np.timedelta64(5, 'h'):
        status("Adjusting for detected difference of", tz_diff)
        force_orig.index += tz_diff

if not check_timeline(force_orig) or not check_timeline(sensor_orig):
    breakpoint()

status("Resampling")
force_resampled = resample(force_orig)
sensor_resampled = resample(sensor_orig)

status("Smoothing")
force = smooth(force_resampled)
sensor = smooth(sensor_resampled).round().astype(int)

force['press'] = detect_presses(force, cmdline.threshold)

placement = np.array([
    [2, 1,  9, 10],
    [4, 3, 11, 12],
    [6, 5, 13, 14],
    [8, 7, 15, 16],
]) - 1

flatplace = placement.flatten()
press_to_cell = { p: flatplace[p] for p in range(flatplace.size) }
cell_to_press = { v: k for k, v in press_to_cell.items() }

adjacent_to_cell = {}
for (row, col), cell in np.ndenumerate(placement):
    for pos in [(row - 1, col), (row + 1, col),
                (row, col - 1), (row, col + 1),
                # (row - 1, col - 1), (row - 1, col + 1),
                # (row + 1, col - 1), (row + 1, col + 1)
    ]:
        if pos[0] < 0 or pos[1] < 0: continue
        try:
            adjacent_to_cell.setdefault(cell, []).append(placement[pos])
        except IndexError:
            pass

def align_score(X, Y, patch=1):
    if len(X) == 0 or len(Y) == 0:
        return 0
    presses = X.press.dropna().unique().astype(int)
    r2 = []
    score = 0
    for press_num in [0]:#presses:
        press = X.press == press_num
        if not press.any():
            continue
        x = X[press]['force'].values.reshape(-1, 1)
        y = Y[press][patch, flatplace[press_num]].values
        lin = LinearRegression().fit(x, y)
        r2 = metrics.r2_score(y, lin.predict(x))
        score += r2
    print(score)
    return score

def align(f, s, offset=0):
    if type(offset) == np.ndarray and len(offset) == 1:
        offset = offset[0].astype(int)
    if type(s.columns) == pd.core.indexes.multi.MultiIndex:
        s.columns = s.columns.to_flat_index()
    f.index -= f.index.min()
    f.index += s.index.min() + np.timedelta64(offset, 'ms')
    index = s.index.intersection(f.index)
    return f.loc[index], s.loc[index]
        
def plot_vs(force, sensor, offset=0):
    f = force.copy()
    #s.index -= sensor.index.min()
    #f.index = f.index - f.index.min() + np.timedelta64(offset, 'ms')
    #y = s[s.index.isin(f.index)]
    if type(sensor.columns) == pd.core.indexes.multi.MultiIndex:
        sensor.columns = sensor.columns.to_flat_index()
    x, y = align(f, sensor, offset)
    lines = [plt.plot(x['force'], y[col], '-', lw=0.1, c='k', label=col)[0] for col in y.columns]
    return plt.gcf(), (f, sensor, lines)

def plot_vs_update(offset, args):
    f, sensor, lines = args
    #f.index = f.index - f.index.min() + np.timedelta64(1000*offset, 'ms')
    #y = s[s.index.isin(f.index)]
    x, y = align(f, sensor, offset*1000)
    # if len(y) == 0:
    #     global anim
    #     anim.event_source.stop()
    #     print("Stopped")
    #     return
    for i, line in enumerate(lines):
        line.set_ydata(y.iloc[:, i])
    #print(offset, align_score(x, y))


def alignment_animation(force, sensor):
    fig, args = plot_vs(force, sensor)
    anim = FuncAnimation(fig, func=plot_vs_update, fargs=(args,), interval=10)
    plt.show()



# max_offset = int((sensor.index.max() - sensor.index.min()).total_seconds()*1000)

# def inverted_score(offset_ratio):
#     x, y = align(force, sensor, offset_ratio*max_offset)
#     return -align_score(x, y)

# # def inverted_score(offset):
# #     x, y = align(force, sensor, offset)
# #     return -align_score(x, y)

# def in_range(**kwargs):
#     x = kwargs['x_new']
#     return 0.0 <= x <= 1.0

#status("Aligning")
# from scipy.optimize import minimize, basinhopping
# #res = minimize(inverted_score, [0], bounds=((0, max_offset),))
# res = basinhopping(inverted_score, [0], niter=100, accept_test=in_range)
