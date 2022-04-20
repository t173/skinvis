#!/usr/bin/env python3

import sys
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from sklearn.linear_model import LinearRegression

parser = argparse.ArgumentParser()
parser.add_argument('infiles', nargs='+', help='input calibration test CSVs')
parser.add_argument('--output', '-o', required=True, help='save calibration profile to file')
parser.add_argument('--plot', '-p', help='save sensitivity plot')
parser.add_argument('--fit', '-f', help='save model fit plot')
parser.add_argument('--fmt', default='pdf', help='save plot using format FMT')
parser.add_argument('--figsize', nargs=2, default=(4, 3), help='figure size')
parser.add_argument('--model', choices=['linear', 'quadratic'], default='linear', help='interpolation model')
cmdline = parser.parse_args()

num_files = len(cmdline.infiles)

dfs = []
for num, infile in enumerate(cmdline.infiles):
    df = pd.read_csv(infile).set_index(['patch', 'cell'])
    df.columns = [col + str(num) for col in df.columns]
    dfs.append(df)
    
df = pd.concat(dfs, axis=1)

df['zero_avg'] = df[['baseline' + str(n) for n in range(num_files)]].mean(axis=1).round().astype(int)
df['zero_std'] = df[['baseline' + str(n) for n in range(num_files)]].std(axis=1)

for n in range(num_files):
    df['delta' + str(n)] = (df['activated' + str(n)] - df['baseline' + str(n)])/df['force' + str(n)]


def plot_setup():
    plt.figure(figsize=cmdline.figsize)
    ax = plt.gca()
    plt.subplots_adjust(left=0.15, right=0.95, top=0.93)
    for spine in ax.spines:
        ax.spines[spine].set_visible(False)

def plot_sensitivity(df):
    plot_setup()
    X = np.zeros((len(df), 1))
    Y = np.zeros((len(df), 1), dtype=int)
    for n in range(num_files):
        x = df['force' + str(n)].values.reshape(-1, 1)
        y = (df['activated' + str(n)] - df['baseline' + str(n)]).round().astype(int).values.reshape(-1, 1)
        X = np.concatenate([X, x], axis=1)
        Y = np.concatenate([Y, y], axis=1)
    plt.plot(X.T, Y.T, '-o', label=df.index)
    #plt.title("Sensitivity", fontsize=14)
    for cell in range(len(X)):
        #plt.text(X[cell, -1], Y[cell, -1], '  ' + str(cell))
        plt.annotate('  ' + str(cell), (X[cell, -1], Y[cell, -1]), xytext=(5, 0), textcoords='offset points')
    plt.xlabel("Indentation force (N)", fontsize=12)
    plt.ylabel("Change in sensor value", fontsize=12)

def save_fit(df, patch, cell, filebase=None):
    linreg = LinearRegression()
    def quadratic_fit(x, y):
        x1 = x.reshape(-1, 1)
        x2 = x1*x1
        X = np.concatenate([x2, x1], axis=1)
        res = linreg.fit(X, y)
        samples_x = np.linspace(x.min(), x.max()).reshape(-1, 1)
        # samples_y = res.coef_[0]*samples_x*samples_x + res.coef_[1]*samples_x + res.coef_[2]
        # return samples_x, samples_y
        samples_X = np.concatenate([samples_x*samples_x, samples_x], axis=1)
        return samples_x, linreg.predict(samples_X), res

    def linear_fit(x, y):
        X = x.reshape(-1, 1)
        res = linreg.fit(X, y)
        samples_x = np.array([[x.min()], [x.max()]])
        return samples_x, linreg.predict(samples_x), res

    plot_setup()

    # Note swapped axes from sensitivity
    plt.xlabel("Change in sensor value", fontsize=12)
    plt.ylabel("Indentation force (N)", fontsize=12)

    row = df.loc[(patch, cell), :]
    force = ['force%d' % n for n in range(num_files)]
    baseline = ['baseline%d' % n for n in range(num_files)]
    activated = ['activated%d' % n for n in range(num_files)]
    num_points = len(force) + 1
    x = np.zeros((num_points,))
    y = np.zeros((num_points,))
    x[1:] = row[activated].values - row[baseline].values
    y[1:] = row[force].values
    
    color = 'C%d' % cell
    X, Y, linear_res = linear_fit(x, y)
    linear_model = [linear_res.intercept_, linear_res.coef_[0], 0]
    plt.plot(X, Y, '--', label='linear', c=color, zorder=1)

    X, Y, quadratic_res = quadratic_fit(x, y)
    quadratic_model = [quadratic_res.intercept_, quadratic_res.coef_[1], quadratic_res.coef_[0]]
    plt.plot(X, Y, '-', label='quadratic', c=color, zorder=2)

    if cmdline.model == 'linear':
        model = linear_model
    elif cmdline.model == 'quadratic':
        model = quadratic_model
    else:
        raise ValueError('Unknown model: ' + str(cmdline.type))

    plt.scatter(x, y, marker='o', c=color, zorder=10)
    plt.legend(frameon=False)
    #result = stats.linregress(X.T, y)
    plt.title('Cell %d' % cell)
    if filebase:
        filename = '%s-cell%s.%s' % (filebase, cell, cmdline.fmt)
        print("Saving", filename)
        plt.savefig(filename, fmt=cmdline.fmt, bbox_inches='tight')

    return model

plot_sensitivity(df)
if cmdline.plot:
    print("Saving", cmdline.plot)
    plt.savefig(cmdline.plot, fmt=cmdline.fmt, bbox_inches='tight')

rows = []
for patch, cell in df.index.values:
    model = save_fit(df, patch, cell, cmdline.fit)
    baseline = df.loc[(patch, cell), 'baseline0'].astype(int)
    rows.append([patch, cell, baseline] + model)
    # if cell == 4:
    #     breakpoint()
profile = pd.DataFrame(rows, columns=['patch', 'cell', 'baseline', 'c0', 'c1', 'c2']).set_index(['patch', 'cell'])
profile.to_csv(cmdline.output)
        
