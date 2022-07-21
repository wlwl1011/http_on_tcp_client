sudo route del default dev eth0
sudo route del default dev wlo1
sudo route add default gw 192.168.0.1 dev wlo1 metric 600
sudo route add default gw 172.20.10.1 dev eth0 metric 50