#!/bin/bash
set -ex
if [[ $1 != "debug" ]] && [[ $1 != "release" ]]; then
    echo "First command line argument should be debug or release"
    exit 1
fi	
CMAKE_BUILD_TYPE=$1
SCRIPT_FOLDER=$(dirname "${0}") 
WORKSPACE_FOLDER=$(readlink -e "${SCRIPT_FOLDER}/../../")
BUILD_FOLDER="${WORKSPACE_FOLDER}/build/${CMAKE_BUILD_TYPE}"
BUILD_DATA_FOLDER="${WORKSPACE_FOLDER}/build/${CMAKE_BUILD_TYPE}/data"
BUILD_TEST_DATA_FOLDER="${WORKSPACE_FOLDER}/build/${CMAKE_BUILD_TYPE}/test/data"
TRAIN_FOLDER="${WORKSPACE_FOLDER}/data/train"
SOURCE_FOLDER="${WORKSPACE_FOLDER}/source"
PYTHON_FOLDER="${SOURCE_FOLDER}/python"
CMAKE_CXX_COMPILER=g++
CMAKE_MAKE_PROGRAM=ninja
USE_BACKEND=OPENCL 
USE_TCMALLOC=1 
USE_AVX2=1 

