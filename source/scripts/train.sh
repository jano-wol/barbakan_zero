#!/bin/bash
set -ex
if [ "$#" -ne 1 ]; then
    echo "Illegal number of parameters."
fi
source "$(dirname "${0}")/build/init.sh"
source "$(dirname "${0}")/train/train.sh ${1}"

