#!/bin/bash
set -ex

COMPARE_NNUE_DATA_IN=${BUILD_TEST_DATA_FOLDER}/compare_nnue_output/
COMPARE_POS_LEN=20
source ${PYTHON_FOLDER}/venv/bin/activate
python3 ${PYTHON_FOLDER}/test_model_output.py -checkpoint ${COMPARE_NNUE_DATA_IN}model.ckpt -pos-len ${COMPARE_POS_LEN} -batch-size 1 -out-file-value ${COMPARE_NNUE_DATA_IN}nn_py_value.bin -out-file-policy ${COMPARE_NNUE_DATA_IN}nn_py_policy.bin -out-file-nnue-weights ${COMPARE_NNUE_DATA_IN}zero.nnue
${BUILD_FOLDER}/bin/barbakan_zero testnnueoutput ${COMPARE_POS_LEN}


