#!/bin/bash
set -ex

source ${PYTHON_FOLDER}/venv/bin/activate
SELFPLAY_FOLDER="${BUILD_TEST_DATA_FOLDER}/selfplay_test"
rm -rf "${SELFPLAY_FOLDER}"
mkdir -p "${SELFPLAY_FOLDER}"
source $(dirname "${0}")/test/synchronous_loop_test.sh np "${SELFPLAY_FOLDER}" selfplay_test b2c16 1

SHUFFLE_OUT="${OUTPUT_FOLDER}/shuffle"
DUMP_FILE="${OUTPUT_FOLDER}/dump_positions_out"
python3 ${PYTHON_FOLDER}/generate_nnue_data/shuffle.py ${INPUT_SELFPLAY_FOLDER} -expand-window-per-row 1.0 -taper-window-exponent 1.0 -out-dir ${SHUFFLE_OUT} -out-tmp-dir ${OUTPUT_FOLDER}/shuffle_tmp -num-processes 4 -batch-size 128 -keep-target-rows ${TARGET_SHUFFLE} -min-rows 20000 -output-npz
python3 ${PYTHON_FOLDER}/generate_nnue_data/dump_positions.py -shuffle-dir ${SHUFFLE_OUT} -dump-file ${DUMP_FILE} -batch-size 128 -pos-len ${POS_LEN} -target ${TARGET_NNUE_DATA_ROWS}
${NN_ENGINE_FOLDER}/barbakan_zero testnnueoutput ${OUTPUT_FOLDER} ${POS_LEN}


