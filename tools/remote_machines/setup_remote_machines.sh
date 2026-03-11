#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

$SCRIPT_DIR/sync_remote_machines.sh

echo
echo "*********************************************"
echo "Setting up geodude"
echo "*********************************************"
echo

ssh geodude "source ~/.bashrc && source ~/.profile && ~/synapse-project/tools/setup_geodude.sh"

echo
echo "*********************************************"
echo "Setting up graveler"
echo "*********************************************"
echo

ssh graveler "source ~/.bashrc && source ~/.profile && ~/synapse-project/tools/setup_graveler.sh"

echo
echo "*********************************************"
echo "Setting up tofino1"
echo "*********************************************"
echo

ssh tofino1 "source ~/.bashrc && source ~/.profile && ~/synapse-project/tools/setup_tofino1.sh"

echo
echo "*********************************************"
echo "Setting up tofino2"
echo "*********************************************"
echo

ssh tofino2 "source ~/.bashrc && source ~/.profile && ~/synapse-project/tools/setup_tofino2.sh"