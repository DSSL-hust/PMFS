#!/bin/sh
#./uio_setup.sh
umount /mnt/pmem_emul
rmmod pmfs

#make clean

#make

insmod pmfs.ko
mount -t pmfs -o init /dev/pmem0 /mnt/pmem_emul
dd if=/dev/zero of=/mnt/pmem_emul/.log bs=1M count=8
