#!/bin/bash
set -ex
source ${PYTHON_FOLDER}/venv/bin/activate
SELFPLAY_FOLDER="${BUILD_TEST_DATA_FOLDER}/selfplay_test"
rm -rf "${SELFPLAY_FOLDER}"
mkdir -p "${SELFPLAY_FOLDER}"
source $(dirname "${0}")/test/synchronous_loop_test.sh np "${SELFPLAY_FOLDER}" selfplay_test b2c16 1

