#!/bin/bash

baud_rate=2000000
devices="/dev/ttyUSB0 /dev/ttyUSB1"
target="/dev/octocan"

function find_device() {
    for device in $devices; do
	if [[ -c $device ]]; then
	    printf "%s" $device
	    return
	fi
    done
    echo "Cannot find octocan device"
    exit 1
}

device=$(find_device)

echo "Found octocan device at $device"
echo "Configuring serial"
stty -F $device raw
stty -F $device -echo -echoe -echok
stty -F $device $baud_rate

echo "Creating $target"
ln -snf $device $target

