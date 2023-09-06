# barbakan
In this repo I will try to adapt KataGo's zero knowledge training pipeline to gomoku. For more info:
https://github.com/lightvector/KataGo

# setup repo
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

# train NN model
After the build step is ready nn model train can be started by:  
./source/scripts/train.sh release train_id

If train_id is a new id, then a new training process will be started. Otherwise, the training process corresponding to the already existing id will be continued. The output folder of the training process is ./data/train/train_id. (This is not cleaned by the configure step.)

Currently train.sh will always train a hard coded NN model corresponding to model id b6c96 (see ./source/python/modelconfigs.py for all the possibilities). 
To change this hard coded value, adjust line  
NN_ID=b6c96  
in   
./source/scripts/train/loop_call.sh  
