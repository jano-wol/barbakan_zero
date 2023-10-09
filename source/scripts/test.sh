#!/bin/bash
set -ex
source "$(dirname "${0}")/build/init.sh"
source "${SOURCE_FOLDER}/scripts/test/quick_train.sh"
source "${SOURCE_FOLDER}/scripts/test/quick_generate_nnue_data.sh"
echo "All tests passed"

