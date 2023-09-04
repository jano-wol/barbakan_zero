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

To check that you have a working repo for development run the followings from the root of the repo:  
./source/scripts/configure.sh release  
./source/scripts/build.sh release  
./source/scripts/test.sh release  
(last message should be 'All tests passed')  
