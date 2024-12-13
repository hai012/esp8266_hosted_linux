insmod  /tmp/uart_wifi.ko                                    
echo "ttyS2" > /sys/bus/platform/devices/uart_wifi0/tty_name      
echo "moto" > /sys/bus/platform/devices/uart_wifi0/ssid       
echo "ghj1234567" > /sys/bus/platform/devices/uart_wifi0/password
ifconfig eth0 up
IPV4ADDR=`cat /sys/bus/platform/devices/uart_wifi0/netif_info|sed -n '1p'`
NETMASK=`cat /sys/bus/platform/devices/uart_wifi0/netif_info|sed -n '2p'`
GATEWAY=`cat /sys/bus/platform/devices/uart_wifi0/netif_info|sed -n '3p'`
ifconfig eth0 ${IPV4ADDR} netmask ${NETMASK}
ip route add default via ${GATEWAY} dev eth0
