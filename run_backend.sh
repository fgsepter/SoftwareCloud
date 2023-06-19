# backend
###
 # @Author: Yixuan Meng yixuanm@seas.upenn.edu
 # @Date: 2023-05-07 23:23:25
 # @LastEditors: Yixuan Meng yixuanm@seas.upenn.edu
 # @LastEditTime: 2023-05-07 23:25:21
 # @FilePath: /T03/run_backend.sh
 # @Description: setting
### 
cd backend
rm kvs_*
make
./master ../common/backend_servers.txt 1 -v &
./main ../common/backend_servers.txt 2 -v &
./main ../common/backend_servers.txt 3 -v &
./main ../common/backend_servers.txt 4 -v &
./main ../common/backend_servers.txt 5 -v &
./main ../common/backend_servers.txt 6 -v &
./main ../common/backend_servers.txt 7 -v