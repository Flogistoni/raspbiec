#!/bin/sh

#usage: instdrv.sh <debuglevel>

debug=0
if [ $1 ] ; then
    debug=$1
fi
sudo rmmod raspbiecdrv
sudo insmod raspbiecdrv.ko debug=$debug
sudo chmod go+rw /dev/raspbiec 
#cat /sys/devices/virtual/raspbiec/raspbiec/state
