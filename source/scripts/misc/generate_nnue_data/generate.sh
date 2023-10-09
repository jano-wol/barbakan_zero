#!/bin/bash
set -ex

INPUT_SELFPLAY_FOLDER=$1
OUTPUT_FOLDER=$2
TARGET_NNUE_DATA_ROWS=$3

SCRIPT_FOLDER_GENERATE_NNUE=$(dirname "${0}") 
WORKSPACE_FOLDER=$(readlink -e "${SCRIPT_FOLDER_GENERATE_NNUE}/../../../../")
SOURCE_FOLDER="${WORKSPACE_FOLDER}/source"
PYTHON_FOLDER="${SOURCE_FOLDER}/python"
source ${PYTHON_FOLDER}/venv/bin/activate
python3 ${PYTHON_FOLDER}/generate_nnue_data/shuffle.py ${INPUT_SELFPLAY_FOLDER} -expand-window-per-row 1.0 -taper-window-exponent 1.0 -out-dir ${OUTPUT_FOLDER}/shuffle -out-tmp-dir ${OUTPUT_FOLDER}/shuffle_tmp -num-processes 4 -batch-size 128 -keep-target-rows ${TARGET_NNUE_DATA_ROWS} -min-rows 20000 -output-npz

