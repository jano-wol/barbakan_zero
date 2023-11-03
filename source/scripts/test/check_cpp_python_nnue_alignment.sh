#!/bin/bash
set -ex

source ${PYTHON_FOLDER}/venv/bin/activate
python3 ${PYTHON_FOLDER}/generate_nnue_data/shuffle.py ${INPUT_SELFPLAY_FOLDER} -expand-window-per-row 1.0 -taper-window-exponent 1.0 -out-dir ${SHUFFLE_OUT} -out-tmp-dir ${OUTPUT_FOLDER}/shuffle_tmp -num-processes 4 -batch-size 128 -keep-target-rows ${TARGET_SHUFFLE} -min-rows 20000 -output-npz
python3 ${PYTHON_FOLDER}/generate_nnue_data/dump_positions.py -shuffle-dir ${SHUFFLE_OUT} -dump-file ${DUMP_FILE} -batch-size 128 -pos-len ${POS_LEN} -target ${TARGET_NNUE_DATA_ROWS}
${NN_ENGINE_FOLDER}/barbakan_zero testnnueoutput ${OUTPUT_FOLDER} ${POS_LEN}


