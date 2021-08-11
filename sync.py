#!/usr/bin/env python3

import sys
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter
from matplotlib.animation import FuncAnimation

force_label = "Indentation force (N)"

other_style = { 'ls': '-', 'lw': 0.5, 'c': '0.7', 'label': 'other' }
down_style = { 'ls': '-', 'lw': 3, 'c': 'tab:blue', 'label': 'press' }
up_style = { 'ls': '-', 'lw': 3, 'c': 'cadetblue', 'label': 'release' }

def parse_cmdline():
    parser = argparse.ArgumentParser()
    parser.add_argument('force', help='input force data')
    parser.add_argument('sensor', help='output sensor data')
    parser.add_argument('--threshold', '-t', type=float, default=None, help='threshold for press detection')
    parser.add_argument('--digits', type=int, default=3, help='number of digits for resolution')
    parser.add_argument('--verbose', '-v', action='store_true', default=True, help='show more progress')
    parser.add_argument('--shift', '-s', metavar='SECONDS', type=float, help='shift sensor in time by SECONDS')
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
    if cmdline.verbose:
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
    if cmdline.shift:
        shift = np.timedelta64(int(cmdline.shift*1e9), 'ns')
        status("Shifting sensor values by", shift/np.timedelta64(1, 's'), "s")
        df.index += shift
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
    return df.rolling(window=size, center=True).mean().dropna()


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

def most_freq(X, smooth='sqrt', hist=False):
    """
    """
    #X_smooth = pd.Series(X).rolling(smooth).mean().dropna().values if smooth else X
    count, edges = np.histogram(X, bins='sqrt', density=True)
    if smooth == 'sqrt':
        smooth = 2*int(0.02*np.sqrt(len(X)))
    if smooth:
        count = pd.Series(count).rolling(smooth, center=True).mean().values
    c = np.nanargmax(count)
    fq = edges[c:c+2].mean()
    return (fq, count, edges) if hist else fq

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
        for x in np.arange(df.force.min().round(cmdline.digits), (df.force.min() + df.force.max())/2, 0.001):
            if cut_threshold(force, x, step).nunique()[0] == expected:
                threshold = x.round(cmdline.digits)
                break
        if threshold is None:
            status("Could not find threshold value")
            breakpoint()
        status("Found threshold value", threshold, "N")
    presses = cut_threshold(df, threshold, step)

    if presses['press'].nunique() != expected:
        status("Warning: Expected", expected, "presses, but found", presses.nunique(), "(check threshold value)")
    return presses

def get_press_times(df):
    """
    Gets press start and stop from previously detected presses in df
    """
    press_data = [ [int(p), group.index.min(), group.index.max()] for p, group in df.groupby('press') ]
    return pd.DataFrame(press_data, columns=['press', 'start', 'stop']).set_index('press')

def get_press_extents(df):
    """
    Gets the extents (most frequent) of the presses in newtons
    """
    d = [[int(p), most_freq(group.force)] for p, group in df.groupby('press')]
    return pd.DataFrame(d, columns=['press', 'extent']).set_index('press')

def plot_presses(df, presses=True, extents=True, figsize=(12, 4)):
    line_style = {
        'color': 'k',
        'linewidth': 1,
        'linestyle': '-',
    }
    extent_style = {
        'color': 'tab:blue',
        'linewidth': 2,
        'linestyle': '--',
    }
    press_style = {
        'color': 'tab:blue',
        'alpha': 0.2,
    }
    plt.figure(figsize=figsize)
    plt.subplots_adjust(left=0.05, right=0.95)
    plt.xticks(rotation=-30, ha='left', va='top')
    plt.plot(df.force, zorder=10, **line_style)
    plt.xlabel("Time", fontsize=14)
    plt.ylabel(force_label, fontsize=14)
    plt.ylim(ymin=0)
    ax = plt.gca()
    ax.spines['right'].set_visible(False)
    ax.spines['top'].set_visible(False)
    exts = get_press_extents(df)
    for p in df.press.dropna().unique():
        #plt.plot(df[df.press == press].force, '-', lw=3, zorder=10, label=int(press))
        press = df[df.press == p]
        start = press.index.min()
        stop = press.index.max()
        if presses:
            plt.axvspan(start, stop, zorder=1, **press_style)
        if extents:
            y = exts.loc[p]
            plt.plot([start, stop], [y, y], **extent_style)
        

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
    plt.ylabel("Raw sensor value", fontsize=12)
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

    fmtr = FuncFormatter(lambda x, pos: '%gs' % (x*1e-9))
    ax.xaxis.set_major_formatter(fmtr)

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


def plot_dist(X, color='tab:blue', xlabel=None, ylabel='Value'):
    freqline_style = {
        'color': 'k',
        'linestyle': '--',
        'linewidth': 2,
    }
    hist_style = {
        'facecolor': color,
        'edgecolor': color,
        'alpha': 0.5,
        'linewidth': 0.5,
    }
    histline_style = {
        'color': color,
        'linestyle': '-',
        'linewidth': 1.5,
    }

    fig = plt.figure(figsize=(9, 3))
    left = plt.subplot(1, 2, 1)
    right = plt.subplot(1, 2, 2, sharey=left)
    #plt.xscale('log')

    for ax in [left, right]:
        for spine in ax.spines:
            ax.spines[spine].set_visible(False)

    #width = int(0.02*np.sqrt(len(X)))
    fq, hist_count, hist_edges = most_freq(X, hist=True)#, smooth=2*width)
    hist_edge_centers = pd.Series(hist_edges).rolling(2).mean().dropna().values

    def on_xlims_changed(ax):
        right.cla()

        # Xr = X[type(X.index[0])(xmin):type(X.index[0])(xmax)]
        # right.hist(Xr, bins='sqrt', histtype='stepfilled', density=True, orientation='horizontal', **hist_style)

        #xmin, xmax = ax.get_xlim()
        #Xzoom = X[type(X.index[0])(xmin):type(X.index[0])(xmax)]
        freq, bins, _ = right.hist(
            #Xzoom,
            X,
            bins='sqrt', density=True,
            histtype='stepfilled',
            orientation='horizontal',
            **hist_style)
        # bin_centers = pd.Series(bins).rolling(2).mean().dropna().values
        # freq_smooth = pd.Series(freq).rolling(2*width).mean().dropna().values
        # right.plot(freq_smooth, bin_centers[width:width + len(freq_smooth)])

        # fq = most_freq(Xzoom, 2*width)
        right.plot(hist_count, hist_edge_centers, **histline_style)
        fq_str = '{:.{digits}f}'.format(fq, digits=round(-int(np.floor(np.log10(np.diff(bins).mean())))))
        right.axhline(fq, label=fq_str, **freqline_style)
        right.set_xlabel('Probability density')
        right.set_xticks([])
        right.legend(frameon=False)

    # Left line plot
    left.set_xlabel(xlabel if xlabel else 'Sequence')
    left.set_ylabel(ylabel)
    if type(X) == pd.core.series.Series or type(X) == pd.core.frame.DataFrame:
        #index = X.index - X.index.min()
        #left.plot(X.index.values.astype(float), X.values, c=color)
        index = X.index
        if xlabel is None and 'time' in str(type(X.index)).lower():
            index = (index - index.min()).total_seconds()
            left.set_xlabel('Time (s)')
            #left.set_xticks([index.min(), index.max()])
        left.plot(index, X.values, c=color)
    else:
        left.plot(X, c=color)
    on_xlims_changed(left)
    left.axhline(fq, **freqline_style)
    #left.callbacks.connect('xlim_changed', on_xlims_changed)

    plt.subplots_adjust(left=0.1, bottom=0.17, right=0.95, top=0.95)
    return fq
    

def calibrate(force, sensor, patch=1):
    presses = get_press_times(force)
    not_pressed = force[force.press.isna()].index
    idle_sensor = sensor.loc[sensor.index.isin(not_pressed), :]
    force_base = most_freq(force[force.press.isna()].force)
    if cmdline.verbose:
        status("Baseline force", force_base, "N")
    data = []
    for p in presses.index:
        cell = press_to_cell[p]
        addr = (patch, cell)
        cell_values = sensor.loc[presses.loc[p].start:presses.loc[p].stop, addr]

        activated = int(most_freq(cell_values).round())
        baseline = int(most_freq(idle_sensor[addr]).round())

        force_active = most_freq(force[force.press == p].force)
        force_applied = force_active - force_base

        data.append([patch, cell, baseline, activated, force_applied])
    data = sorted(data)
    df = pd.DataFrame(data, columns=['patch', 'cell', 'baseline', 'activated', 'force']).set_index(['patch', 'cell'])
    return df
