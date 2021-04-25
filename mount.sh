#!/bin/sh
#./uio_setup.sh
umount /mnt/pmem_emul
rmmod pmfs

#make clean

#make

insmod pmfs.ko
mount -t pmfs -o physaddr=0x1800000000,init=24G /dev/pmem1 /mnt/pmem_emul

touch /mnt/pmem_emul/1.txt
dd if=/root/1.txt of=/mnt/pmem_emul/1.txt bs=1M count=256
dd if=/dev/zero of=/mnt/pmem_emul/log bs=1M count=4
mkdir /mnt/pmem_emul/mmapfileset
#mkdir /mnt/pmem_emul/bigfileset
#dd if=/dev/zero of=/mnt/pmem_emul/file1 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file2 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file3 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file4 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file5 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file6 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file7 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file8 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file9 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file10 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file11 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file12 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file13 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file14 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file15 bs=2M count=128
#dd if=/dev/zero of=/mnt/pmem_emul/file16 bs=2M count=128

#mkdir /mnt/pmem_emul/bigfileset
#touch /mnt/pmem_emul/bigfileset/1.txt
