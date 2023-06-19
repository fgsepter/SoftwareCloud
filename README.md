Team Name: T03
Full name:  Bowen Deng, Yigao Fang, Yixuan Meng, Zhaozheng Shen
SEAS login: dengbw, fgsepter, yixuanm, shenzhzh

Which features did you implement? 
  Entire assignment

Did you complete any extra-credit tasks? If so, which ones?
  (list extra-credit tasks)
  Enhance the user interface to look beautiful

Did you personally write _all_ the code you are submitting
(other than code from the course web page)?
  [-] Yes
  [ ] No

Did you copy any code from the Internet, or from classmates?
  [ ] Yes
  [-] No

Did you collaborate with anyone on this assignment?
  [ ] Yes
  [-] No

## Instuctions to run the code

### 1. Start the backend servers
- Run the following commands to start the backend servers
```
cd backend
rm kvs_*
make
./master ../common/backend_servers.txt 1 -v
./main ../common/backend_servers.txt 2 -v
./main ../common/backend_servers.txt 3 -v
./main ../common/backend_servers.txt 4 -v
./main ../common/backend_servers.txt 5 -v
./main ../common/backend_servers.txt 6 -v
./main ../common/backend_servers.txt 7 -v
```

### 2. Email Service
- Run the following command to start the relay server
```
cd email_service
make
./relay -v . -p 25
```

### 3. Start the frontend servers
- Run the following commands to start the frontend servers
```
cd frontend
make
./load_balancer -v
./httpserver 1
./httpserver 2
./httpserver 3
```

Reference: the encode and decode functions are referenced from https://github.com/ReneNyffenegger/cpp-base64/blob/master/base64.cpp