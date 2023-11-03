#!/bin/bash
set -ex
source "$(dirname "${0}")/build/init.sh"
#source "${SOURCE_FOLDER}/scripts/test/quick_train.sh"
#source "${SOURCE_FOLDER}/scripts/test/quick_generate_nnue_data.sh"
source "${SOURCE_FOLDER}/scripts/test/check_cpp_python_nnue_alignment.sh"
echo "All tests passed"

