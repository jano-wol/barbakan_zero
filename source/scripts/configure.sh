#!/bin/bash
set -ex
source "$(dirname "${0}")/build/init.sh"
rm -rf "${BUILD_FOLDER}"
mkdir -p "${BUILD_FOLDER}"
cd "${BUILD_FOLDER}"
cmake "${WORKSPACE_FOLDER}" "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}" -G "Ninja" "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}" "-DUSE_BACKEND=${USE_BACKEND}" "-DUSE_TCMALLOC=${USE_TCMALLOC}" "-DUSE_AVX2=${USE_AVX2}"

