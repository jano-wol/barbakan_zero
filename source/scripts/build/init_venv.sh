#!/bin/bash
set -ex
python3 -m venv ${PYTHON_FOLDER}/venv
python_ver=$(python3 --version | sed 's![^.]*$!!' | sed 's/.$//' | sed 's/[^ ]* //')
pth_file=${PYTHON_FOLDER}/venv/lib/python${python_ver}/site-packages/barbakan.pth
cat > ${pth_file} << EOF
${PYTHON_FOLDER}
EOF

source ${PYTHON_FOLDER}/venv/bin/activate 
pip install python-chess==0.31.4 pytorch-lightning==1.9.0 torch matplotlib
ver=$(python3 ${SCRIPT_FOLDER}/build/cupy_cuda_version.py | sed 's/\.//g')
pip install cupy-cuda${ver} tensorboard

