#!/bin/bash

set -euox pipefail

sudo ifconfig enp0s29u1u1u2 up || true
sudo $SDE_INSTALL/bin/bf_kpkt_mod_unload $SDE_INSTALL || true
sudo $SDE_INSTALL/bin/bf_kdrv_mod_load $SDE_INSTALL || true