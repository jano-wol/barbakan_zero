# Barbakan_zero
In this repo I will try to adapt KataGo's zero knowledge training pipeline to gomoku. This repo is a truncated version of repo:  
https://github.com/lightvector/KataGo  
As this is an experimental project derived form KataGo, I definitely recommend to investigate the KataGo repository for best practices. This repo is not matured, but helped me produce Barbakan engine, a strong gomoku program,  
competing successfully on gomoku ai tournament:  
https://gomocup.org/


# Setup repo
Supported os: Linux  

Known dependencies:  
cmake  
ninja  
python3.10-venv (python version is most likely flexible)  
cuda toolkit (https://developer.nvidia.com/cuda-toolkit)  
TCMalloc (sudo apt-get install google-perftools && sudo apt -y install libgoogle-perftools-dev)  
zlib (sudo apt-get install zlib1g-dev && sudo apt-get install libzip-dev)

To check that you have a working repo for development run the followings from the root of the repo:  
./source/scripts/configure.sh release  
./source/scripts/build.sh release  
./source/scripts/test.sh release  
(last message should be 'All tests passed')  

Alternatively, it is possible to setup the work environment of the repo with Docker. For more info, please check part "Docker (misc)". 

# Analyze with the engine
Next to the executable (in ./build/release/bin/barbakan_zero) one need to copy a config file (default_gtp.cfg can be found in ./data/configs/gtp) and a model file (model.bin.gz can be found in ./data/model, or one can train a new model with the training pipeline).  

For a graphical interface please set up the q5gomoku project. Clone and follow the Readme.md instructions of:  
https://github.com/jano-wol/q5gomoku

gtp engine can be also used without the graphical interface, but this is very inconvenient. Some example commands:  
./build/release/bin/barbakan_zero gtp  
boardsize 19  
showboard  
play B A19  
play W C1  
showboard

# Train NN model
After the build step is ready nn model train can be started by:  
./source/scripts/train.sh release train_id

If train_id is a new id, then a new training process will be started. Otherwise, the training process corresponding to the already existing id will be continued. The output folder of the training process is ./data/train/train_id. (This is not cleaned by the configure step.)

Currently train.sh will always train a hard coded NN model corresponding to model id b6c96 (see ./source/python/modelconfigs.py for all the possibilities). 
To change this hard coded value, adjust line  
NN_ID=b6c96  
in   
./source/scripts/train/loop_call.sh  

The training process hyperparameters are determined by the following files:  
./data/configs/selfplay/gatekeeper_train.cfg.in  
./data/configs/selfplay/selfplay_train.cfg.in  
./source/scripts/train/synchronous_loop.sh  
It is possible that as the training process evolves, some changes are needed in the constants. (e.g. one can try to experiment with NUM_GAMES_PER_CYCLE or surewinDepth etc.)

# Docker (misc)
Prerequisite: Docker.
From the root of the repo run (sudo might be needed):  
./source/scripts/misc/docker/build.sh  
./source/scripts/misc/docker/run.sh 

In case run.sh produce the following error:  
Error response from daemon: could not select device driver "" with capabilities: [[gpu]].  
try the following commands on the host machine (sudo might be needed):  
apt-get install -y nvidia-container-toolkit  
apt install -y nvidia-docker2   
systemctl daemon-reload  
systemctl restart docker  
and rerun  
./source/scripts/misc/docker/run.sh  

In case run.sh is successful, a new command port is starting in the image. Now git has to be configured in the image by:  
git config --global --add safe.directory /barbakan_zero

Finally, similarly to the conventional setup, it is possible to test that the repository is functioning correctly by calling:  
./source/scripts/configure.sh release  
./source/scripts/build.sh release  
./source/scripts/test.sh release  

