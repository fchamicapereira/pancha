#!/bin/bash

set -euox pipefail

sudo ifconfig enx020000000002 up
sudo $SDE_INSTALL/bin/bf_kpkt_mod_unload $SDE_INSTALL || true
sudo $SDE_INSTALL/bin/bf_kdrv_mod_load $SDE_INSTALL || true
sudo $SDE_INSTALL/bin/bf_fpga_mod_load $SDE_INSTALL || true