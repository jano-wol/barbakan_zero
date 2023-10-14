#!/bin/bash
# example call
# ./source/scripts/misc/generate_nnue_data/generate.sh ./data/train/selfplay/ ./data/train/out/ 10000000 20 ./data/bin/20
set -ex

INPUT_SELFPLAY_FOLDER=$1
OUTPUT_FOLDER=$2
TARGET_NNUE_DATA_ROWS=$3 # ~5% of the rows will form the validation set
POS_LEN=$4
NN_ENGINE_FOLDER=$5

SCRIPT_FOLDER_GENERATE_NNUE=$(dirname "${0}") 
WORKSPACE_FOLDER=$(readlink -e "${SCRIPT_FOLDER_GENERATE_NNUE}/../../../../")
SOURCE_FOLDER="${WORKSPACE_FOLDER}/source"
PYTHON_FOLDER="${SOURCE_FOLDER}/python"
source ${PYTHON_FOLDER}/venv/bin/activate
TARGET_SHUFFLE=$(( TARGET_NNUE_DATA_ROWS + 1000 ))

SHUFFLE_OUT="${OUTPUT_FOLDER}/shuffle"
DUMP_FILE="${OUTPUT_FOLDER}/dump_positions_out"
python3 ${PYTHON_FOLDER}/generate_nnue_data/shuffle.py ${INPUT_SELFPLAY_FOLDER} -expand-window-per-row 1.0 -taper-window-exponent 1.0 -out-dir ${SHUFFLE_OUT} -out-tmp-dir ${OUTPUT_FOLDER}/shuffle_tmp -num-processes 4 -batch-size 128 -keep-target-rows ${TARGET_SHUFFLE} -min-rows 20000 -output-npz
python3 ${PYTHON_FOLDER}/generate_nnue_data/dump_positions.py -shuffle-dir ${SHUFFLE_OUT} -dump-file ${DUMP_FILE} -batch-size 128 -pos-len ${POS_LEN} -target ${TARGET_NNUE_DATA_ROWS}
${NN_ENGINE_FOLDER}/barbakan_zero generatennuedata ${OUTPUT_FOLDER} ${POS_LEN}
