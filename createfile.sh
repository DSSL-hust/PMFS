#!/bin/sh

for i in $( seq 1 1000)
do
	dd if=/dev/zero of=/mnt/pmem_emul/mmapfileset/file${i} bs=1M count=10
done
