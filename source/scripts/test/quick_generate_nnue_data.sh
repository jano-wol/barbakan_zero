#!/bin/bash
set -ex

BINARY_PATH="${BUILD_FOLDER}/bin/barbakan_zero"
MODEL_FOLDER1="${BUILD_TEST_DATA_FOLDER}/selfplay_test/models"
MODEL_FOLDER2="${BUILD_TEST_DATA_FOLDER}/selfplay_test/rejectedmodels"
MODEL_PATH=""
for f in $(find "${MODEL_FOLDER1}" -name '*.bin.gz'); do MODEL_PATH=$f; done
if [ "${MODEL_PATH}" == "" ]; then
    for f in $(find "${MODEL_FOLDER2}" -name '*.bin.gz'); do MODEL_PATH=$f; done
fi
if [ "${MODEL_PATH}" == "" ]; then
    echo "Could not find model in in ${MODEL_FOLDER1} and ${MODEL_FOLDER2}"
    exit 1
fi
CONFIG_PATH="${BUILD_DATA_FOLDER}/configs/gtp/default_gtp.cfg"
GENERATE_NNUE_DATA_OUT=${BUILD_TEST_DATA_FOLDER}/generate_nnue_data_out
if [ ! -d ${GENERATE_NNUE_DATA_OUT} ]; then
  mkdir -p ${GENERATE_NNUE_DATA_OUT};
fi
cp "${BINARY_PATH}" "${GENERATE_NNUE_DATA_OUT}"
cp "${CONFIG_PATH}" "${GENERATE_NNUE_DATA_OUT}"
cp "${MODEL_PATH}" "${GENERATE_NNUE_DATA_OUT}"
${SOURCE_FOLDER}/scripts/misc/generate_nnue_data/generate.sh ${BUILD_TEST_DATA_FOLDER}/generate_nnue_data ${GENERATE_NNUE_DATA_OUT} 300000 20 ${GENERATE_NNUE_DATA_OUT}


