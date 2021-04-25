#!/bin/sh
#./uio_setup.sh
umount /mnt/pmem_emul
rmmod pmfs

#make clean

#make

insmod pmfs.ko
mount -t pmfs -o init /dev/pmem1 /mnt/pmem_emul

