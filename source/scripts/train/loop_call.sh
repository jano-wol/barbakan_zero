#!/bin/bash
set -ex
source ${PYTHON_FOLDER}/venv/bin/activate
SELFPLAY_FOLDER="${TRAIN_FOLDER}/${2}"
if [ ! -d "$SELFPLAY_FOLDER" ]; then
  mkdir -p "${SELFPLAY_FOLDER}"
fi
NN_ID=b6c96
source $(dirname "${0}")/train/synchronous_loop.sh np "${SELFPLAY_FOLDER}" ${2} ${NN_ID} 1

