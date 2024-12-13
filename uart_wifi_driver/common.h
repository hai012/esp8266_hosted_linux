#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/rwlock.h>
#include <linux/device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/if_link.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/if_arp.h>
#include <linux/string.h>
#include <linux/ip.h>

#ifndef N_DEVELOPMENT
    #define N_DEVELOPMENT	29	/* Manual out-of-tree testing */
#endif
//NET_SKB_PAD
//NET_IP_ALIGN   0 or 2
//ETH_HLEN 14    dest_mac 、src_mac 、type
//ETH_DATA_LEN   1500
//ETH_FCS_LEN 4  tap dev doesn't have FCS
#define MAX_RX_SKB_LINEAR_BUFF_LEN \
(SKB_DATA_ALIGN((NET_SKB_PAD  +  NET_IP_ALIGN + ETH_HLEN + ETH_DATA_LEN))\
+ SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

                               
#define ETH_HEADER_OFFSET_IN_LINEAR_BUFF (NET_SKB_PAD  +  NET_IP_ALIGN)
#define MAX_RECV_LEN (SKB_DATA_ALIGN(ETH_HLEN) + MTU_LEN + 1)

#define SPLIT_DATA 0x55
#define SPLIT_CMD 0xaa
#define MTU_LEN 1500
#define MAX_FRAME_LEN (2+MTU_LEN+1)//[2byte data_len][n byte data][1byte split]
//#define UART_FIFO_LEN 128
#define STATUS_LEN 0
#define STATUS_DATA 1
#define STATUS_SPLIT 2

#define WPA_MAX_SSID_LEN 32
#define WPA_MAX_PASSWD_LEN 64

struct uart_wifi_struct {
    struct platform_device *pdev;

    struct tty_struct *tty;
    spinlock_t lock_uart_write;
    rwlock_t rwlock_attr;
    char tty_name[10];
    char ssid[2+WPA_MAX_SSID_LEN+1];
    char password[2+WPA_MAX_PASSWD_LEN+1];
    int16_t ssid_len;
    int16_t password_len;

    int16_t uart_recv_status;
    int16_t uartDataLen;
    struct sk_buff* recv_skb;

    struct net_device* netdev;
    atomic64_t tx_packets;
    atomic64_t tx_bytes;
    atomic64_t rx_packets;
    atomic64_t rx_bytes;

    

    int gpio_reset;
    int gpio_intr;
    int irq_num_gpio_intr;

    struct work_struct work;
    int work_hold;
    

    char netif_info[2+12+1 + 12*4+1];
    int16_t netif_info_recv_len;
    char netif_info_vaild;
    wait_queue_head_t netif_info_wait_queue;

    char force_reset;
};




extern struct tty_ldisc_ops ttyio_ldisc_ops;
int ttyio_init(struct uart_wifi_struct *uart_wifi);
void uart_wifi_receive(struct net_device* netdev,const char *buf, int len);
int netdev_init(struct uart_wifi_struct *uart_wifi);
void ttyio_write(struct uart_wifi_struct *uart_wifi,const unsigned char *buf, size_t cnt);
void dump(struct device * dev,const char *buf, int len);
void recv_dump(struct device * dev,const char *buf, int len);
void recv_dump(struct device * dev,const char *buf, int len);
void recv_dump_skb(struct device * dev,struct sk_buff *skb);
void send_dump_skb(struct device * dev,struct sk_buff *skb);