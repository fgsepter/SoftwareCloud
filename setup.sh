# backend
cd backend
make
gnome-terminal -- ./master ../common/backend_servers.txt 1 -v
gnome-terminal -- ./main ../common/backend_servers.txt 2 -v
gnome-terminal -- ./main ../common/backend_servers.txt 5 -v
gnome-terminal -- ./main ../common/backend_servers.txt 5 -v

cd ../email_service
gnome-terminal -- ./relay -v . -p 25

# frontend
cd ../frontend
make
gnome-terminal -- ./httpserver 1
gnome-terminal -- ./httpserver 2
gnome-terminal -- ./load_balancer

