#!/bin/bash
set -ex
source ${PYTHON_FOLDER}/venv/bin/activate
SELFPLAY_FOLDER="${BUILD_TEST_DATA_FOLDER}/selfplay_test"
rm -rf "${SELFPLAY_FOLDER}"
mkdir -p "${SELFPLAY_FOLDER}"
source ${PYTHON_FOLDER}/selfplay/synchronous_loop_quick.sh np "${SELFPLAY_FOLDER}" selfplay_test b6c96 1

