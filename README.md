# skinvis
Visualization for skin sensor testing board

This is a simple matplotlib based scrtipt for visualization of data from a testing board for a tactile sensor project.  This uses Python 3 and requires the following packages:
  - [numpy](https://numpy.org/)
  - [matplotlib](https://matplotlib.org/)
  - [serial](https://pypi.org/project/pyserial/)
  
Both *numpy* and *matplotlib* are included in the [scipy](https://www.scipy.org/) distribution.  It is preferable to install them using your distribution's package manager and official repositories.  For Ubuntu, you can do this using
```
$ sudo apt install python3-scipy python3-serial
```
Alternatively you can install them using pip:
```
$ python3 -m pip install scipy serial
```
Once installed, you can run the script using
```
$ python3 skinvis.py
```
There are a number of command line options available; use `-h` to list them.  In particular, you may wish to specify the serial port (`-p` option):
```
$ python3 skinvis.py -p /dev/ttyUSB0
```
