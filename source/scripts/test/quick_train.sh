#!/bin/bash
set -ex
source ${PYTHON_FOLDER}/venv/bin/activate
source ${PYTHON_FOLDER}/selfplay/synchronous_loop_quick.sh np ~/Data/katagomoku_nets/tn3_dev tn3_dev b6c96 1

