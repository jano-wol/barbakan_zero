#!/bin/bash
set -ex
source ${PYTHON_FOLDER}/venv/bin/activate
SELFPLAY_FOLDER="${TRAIN_FOLDER}/${1}"
rm -rf "${SELFPLAY_FOLDER}"
mkdir -p "${SELFPLAY_FOLDER}"
source $(dirname "${0}")/test/synchronous_loop_test.sh np "${SELFPLAY_FOLDER}" selfplay_test b6c96 1

