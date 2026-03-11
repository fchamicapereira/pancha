#!/bin/bash

set -euox pipefail

sudo modprobe uio

pushd /opt/dpdk-kmods/linux/igb_uio
    make clean
    make
    sudo insmod igb_uio.ko || true
popd

sudo dpdk-devbind.py -b igb_uio 0000:d8:00.0 0000:d8:00.1

sudo dpdk-hugepages.py --unmount
sudo dpdk-hugepages.py --clear
sudo dpdk-hugepages.py --setup 20G