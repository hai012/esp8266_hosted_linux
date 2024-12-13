# esp8266_hosted_linux




The ESP8266 is used as a network card for Linux, where the Linux host connects to the ESP8266 via a serial port with flow control. The ESP8266 then connects to Wi-Fi to access the internet. The Linux driver is implemented based on the standard interfaces of the serial/tty/net subsystems, making it portable to any Linux development board with a serial port that supports flow control.



Currently, it has been successfully run on Allwinner F1C100S Linux. The computer uses iperf3 to access the development board for speed testing as follows:

```
$ iperf3 -c 192.168.33.55 -R
Connecting to host 192.168.33.55, port 5201
Reverse mode, remote host 192.168.33.55 is sending
[  5] local 192.168.200.128 port 46056 connected to 192.168.33.55 port 5201
[ ID] Interval           Transfer     Bitrate
[  5]   0.00-1.00   sec  34.2 KBytes   280 Kbits/sec
[  5]   1.00-2.00   sec  41.3 KBytes   339 Kbits/sec
[  5]   2.00-3.00   sec  42.8 KBytes   350 Kbits/sec
[  5]   3.00-4.00   sec  39.9 KBytes   327 Kbits/sec
[  5]   4.00-5.00   sec  39.9 KBytes   327 Kbits/sec
[  5]   5.00-6.00   sec  44.2 KBytes   362 Kbits/sec
[  5]   6.00-7.00   sec  39.9 KBytes   327 Kbits/sec
[  5]   7.00-8.00   sec  39.9 KBytes   327 Kbits/sec
[  5]   8.00-9.00   sec  45.6 KBytes   374 Kbits/sec
[  5]   9.00-10.00  sec  39.9 KBytes   327 Kbits/sec
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.21  sec   471 KBytes   378 Kbits/sec  103             sender
[  5]   0.00-10.00  sec   408 KBytes   334 Kbits/sec                  receiver


$ iperf3 -c 192.168.33.55
Connecting to host 192.168.33.55, port 5201
[  5] local 192.168.200.128 port 53142 connected to 192.168.33.55 port 5201
[ ID] Interval           Transfer     Bitrate         Retr  Cwnd
[  5]   0.00-1.00   sec   855 KBytes  7.00 Mbits/sec    0   68.4 KBytes
[  5]   1.00-2.00   sec  0.00 Bytes  0.00 bits/sec    0   25.7 KBytes
[  5]   2.00-3.00   sec  0.00 Bytes  0.00 bits/sec    0   14.3 KBytes
[  5]   3.00-4.00   sec  0.00 Bytes  0.00 bits/sec    0   17.1 KBytes
[  5]   4.00-5.00   sec  0.00 Bytes  0.00 bits/sec    0   22.8 KBytes
[  5]   5.00-6.00   sec   282 KBytes  2.31 Mbits/sec    0   25.7 KBytes
[  5]   6.00-7.00   sec  0.00 Bytes  0.00 bits/sec    0   29.9 KBytes
[  5]   7.00-8.00   sec  0.00 Bytes  0.00 bits/sec    0   22.8 KBytes
[  5]   8.00-9.00   sec  0.00 Bytes  0.00 bits/sec    0   22.8 KBytes
[  5]   9.00-10.00  sec  0.00 Bytes  0.00 bits/sec    0   25.7 KBytes
- - - - - - - - - - - - - - - - - - - - - - - - -
[ ID] Interval           Transfer     Bitrate         Retr
[  5]   0.00-10.00  sec  1.11 MBytes   932 Kbits/sec    0             sender
[  5]   0.00-10.09  sec   395 KBytes   321 Kbits/sec                  receiver
```



The F1C100S Linux serial driver I am using currently does not support DMA. The baudrate is set to 921600, and the F1C100S and ESP8266 are directly connected using DuPont wires in the hardware. When ported to other higher-performance Linux development boards, or by improving the serial noise resistance routing on the hardware PCB, network performance should be further improved.


uart_wifi_passthrough: esp8266 bins, app source, lwip patch for ESP8266_RTOS_SDK, see https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html to build.

uart_wifi_driver: linux host driver for esp8266

the dts you can get here: https://github.com/hai012/f1c100s_lichee_nano_linux/blob/nano-4.14-exp/arch/arm/boot/dts/suniv-f1c100s-licheepi-nano.dts
