#!/bin/bash
set -ex
python3 -m venv ${PYTHON_FOLDER}/venv
python_ver=$(python3 --version | sed 's![^.]*$!!' | sed 's/.$//' | sed 's/[^ ]* //')
pth_file=${PYTHON_FOLDER}/venv/lib/python${python_ver}/site-packages/barbakan.pth
cat > ${pth_file} << EOF
${PYTHON_FOLDER}
EOF

source ${PYTHON_FOLDER}/venv/bin/activate 
pip install psutil numpy torch packaging

