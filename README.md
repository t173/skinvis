# skinvis
Visualization for skin sensor testing board

This is a simple matplotlib based scrtipt for visualization of data from a testing board for a tactile sensor project.  This uses Python 3 and requires the following packages:
  - [numpy](https://numpy.org/)
  - [matplotlib](https://matplotlib.org/)
  - [serial](https://pypi.org/project/pyserial/)
  
Both *numpy* and *matplotlib* are included in the [scipy](https://www.scipy.org/) distribution.  It is preferable to install them using your distribution's package manager and official repositories.  Alternatively you can install them using pip:
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

## Notes for Ubuntu
It is usually preferable to install python packages using your Linux distribution's package manager.  For Ubuntu, you can do this using
```
$ sudo apt install python3-numpy python3-matplotlib python3-serial
```
In order for the `serial` package to work, you will need permission to use the serial device.  Check your system group memberships with
```
$ groups
```
If you are not in the `dialout` group, you may need to add yourself using
```
$ sudo adduser $(whoami) dialout
```
where `$(whoami)` evaluates to your user name.  Log in again for the change to take effect.
