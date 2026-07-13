# Docker creates a parallel network and also touches Iptables. Hence add,
sudo iptables -I FORWARD 1 -s 10.0.0.0/24 -j ACCEPT
sudo iptables -I FORWARD 1 -d 10.0.0.0/24 -j ACCEPT
sudo iptables -I FORWARD 1 -s 192.168.8.0/24 -j ACCEPT
sudo iptables -I FORWARD 1 -d 192.168.8.0/24 -j ACCEPT
sudo iptables -I FORWARD 1 -s 10.0.2.15/24 -j ACCEPT
sudo iptables -I FORWARD 1 -d 10.0.2.15/24 -j ACCEPT

sudo nmcli dev set enp0s3 managed yes
#sudo ip addr add 192.168.0.40 brd 192.168.0.255 dev enp0s8

#sudo nmcli dev set enp0s8 managed no
#sudo ip addr add 10.0.0.1/24 brd 10.0.0.255 dev enp0s8

sudo bash -c 'echo 1 > /proc/sys/net/ipv4/ip_forward'
sudo iptables -t nat -A POSTROUTING -o enp0s3 -j MASQUERADE

#sudo ip route add default via 192.168.0.40 dev enp0s3
