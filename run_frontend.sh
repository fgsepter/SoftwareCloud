# frontend
###
 # @Author: Yixuan Meng yixuanm@seas.upenn.edu
 # @Date: 2023-05-07 23:24:18
 # @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 # @LastEditTime: 2023-05-07 23:26:04
 # @FilePath: /T03/run_frontend.sh
 # @Description: setting
### 
cd frontend
make
./load_balancer &
./httpserver 1 &
./httpserver 2 &
./httpserver 3
