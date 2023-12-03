FROM pytorch/pytorch:2.1.0-cuda12.1-cudnn8-runtime
RUN apt -qq update && DEBIAN_FRONTEND=noninteractive apt -qq install build-essential python3-pip cmake gcc-8 g++-8 git -y
RUN apt install -y nvidia-cuda-toolkit
RUN apt install ninja-build
RUN apt-get install -y google-perftools --fix-missing && apt -y install libgoogle-perftools-dev --fix-missing
RUN apt-get install -y zlib1g-dev && apt-get install -y libzip-dev
RUN apt install -y python3.8-venv
WORKDIR /barbakan_zero

