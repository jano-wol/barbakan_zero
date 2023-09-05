#!/bin/bash
set -ex
source "$(dirname "${0}")/build/init.sh"
source "$(dirname "${0}")/test/quick_train.sh"
echo "All tests passed"

