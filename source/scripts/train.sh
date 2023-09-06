#!/bin/bash
set -ex
if [ "$#" -ne 2 ]; then
    echo "Illegal number of parameters. Example train call should look like e.g: ./source/scripts/train.sh release a_train_id"
    exit 1
fi
source "$(dirname "${0}")/build/init.sh"
source "$(dirname "${0}")/train/loop_call.sh"

