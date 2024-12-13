#include "common.h"


static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    struct uart_wifi_struct* uart_wifi = dev_id;
    schedule_work(&uart_wifi->work);
    return IRQ_HANDLED;
}

static void esp8266ex_init_work_handler(struct work_struct *wk)
{
    struct uart_wifi_struct* uart_wifi = container_of(wk, struct uart_wifi_struct, work);
    struct device *dev = &uart_wifi->pdev->dev;
    struct tty_struct* tty = uart_wifi->tty;
    uint16_t ssid_len = *((uint16_t *)uart_wifi->ssid);
    uint16_t password_len = *((uint16_t *)uart_wifi->password);
    uint16_t i;
    int gpio_value;

    while(1) {
        if(uart_wifi->force_reset) {
            uart_wifi->force_reset =0;
            dev_err(dev,"force_reset");
        } else {
            gpio_value = gpio_get_value(uart_wifi->gpio_intr); 
            if (gpio_value == 0) {
                dev_err(dev,"netif_carrier_ok");
                netif_carrier_on(uart_wifi->netdev);
                netif_wake_queue(uart_wifi->netdev);
                return;
            }
            uart_wifi->uart_recv_status = STATUS_LEN;
            uart_wifi->netif_info_recv_len = 0;
            uart_wifi->netif_info_vaild=0;
            dev_err(dev,"netif carrier is not ok");
        }
        
        tty->ops->flush_buffer(tty);

        gpio_set_value(uart_wifi->gpio_reset,0);
        msleep(10);
        gpio_set_value(uart_wifi->gpio_reset,1);
        msleep(1000);

        dev_err(dev,"ssid:%d %s",ssid_len,uart_wifi->ssid+2);
        tty->ops->write(tty, uart_wifi->ssid, ssid_len+3);
        dev_err(dev,"password:%d %s",password_len,uart_wifi->password+2);
        tty->ops->write(tty, uart_wifi->password, password_len+3);
        for(i=0;i<10;++i) {//超过10s还未检测到下降沿中断函数就尝试再次
            msleep(1000);
            if(!uart_wifi->work_hold)
                return;
            // if(uart_wifi->netif_info_valid){
            //     dev_err(dev,"send netif_info");
            //     tty->ops->write(tty, uart_wifi->netif_info, sizeof(uart_wifi->netif_info));
            //     uart_wifi->netif_info_valid = 0;
            // }
        }
    }
}

int	uart_wifi_open(struct net_device *netdev)
{
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(&netdev->dev);
    uint16_t ssid_len = *((uint16_t *)uart_wifi->ssid);
    uint16_t password_len = *((uint16_t *)uart_wifi->password);
    struct device *dev = &uart_wifi->pdev->dev;
    int ret;

    

    if(ssid_len==0) {
        dev_err(dev,"ssid_len==0");
        return -EINVAL;
    }
    if(password_len==0) {
        dev_err(dev,"password_len==0");
        return -EINVAL;
    }

    netif_carrier_off(netdev);
    if(ttyio_init(uart_wifi)) {
        dev_err(dev,"uart_init failed");
        return -EINVAL;
    }

    uart_wifi->work_hold = 1;
    uart_wifi->force_reset=1;
    uart_wifi->uart_recv_status = STATUS_LEN;
    uart_wifi->netif_info_recv_len = 0;
    uart_wifi->netif_info_vaild=0;
    INIT_WORK(&uart_wifi->work, esp8266ex_init_work_handler);
    ret = request_irq(uart_wifi->irq_num_gpio_intr, gpio_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, "gpio_intr", uart_wifi);
    if (ret) {
        dev_err(dev,"Failed to request IRQ for uart_wifi->irq_num_gpio_intr: %d\n", uart_wifi->irq_num_gpio_intr);
        return ret;
    }
    schedule_work(&uart_wifi->work);
    return 0;
}
int	uart_wifi_stop(struct net_device *netdev)
{

    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(&netdev->dev);
    struct device *dev = &uart_wifi->pdev->dev;
    dev_err(dev,"uart_wifi_stop");

    gpio_set_value(uart_wifi->gpio_reset,0);//取消片选

    free_irq(uart_wifi->irq_num_gpio_intr,uart_wifi);

    netif_carrier_off(netdev);
    netif_stop_queue(netdev);
    uart_wifi->netif_info_vaild = 0;
    uart_wifi->work_hold = 0;
    cancel_work_sync(&uart_wifi->work);
    tty_kclose(uart_wifi->tty);
    return 0;
}

netdev_tx_t	uart_wifi_xmit(struct sk_buff *skb,struct net_device *netdev)
{
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(&netdev->dev);
    struct tty_struct* tty = uart_wifi->tty;
    struct device * dev = &uart_wifi->pdev->dev;
    struct skb_shared_info *shinfo;
    int i;
    char endflag=SPLIT_DATA;
    uint16_t send_len;

    dev_err(dev,"uart_wifi_xmit:skb->len=%d",skb->len);
    send_len = skb->len - ETH_HLEN;
    if(tty->ops->write_room(tty) < send_len + 3) {
        netif_stop_queue(netdev);
        set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
        dev_err(dev,"write_room: %d send_len:%d",tty->ops->write_room(tty), tty->ops->chars_in_buffer(tty));
        return NETDEV_TX_BUSY;
    }
    skb_pull_inline(skb, ETH_HLEN);

    //send_dump_skb(dev,skb);
    //*((uint16_t *)skb->data) = send_len;
    tty->ops->write(tty, (const unsigned char *)&send_len, sizeof(send_len));

    tty->ops->write(tty, skb->data, skb_headlen(skb));
    shinfo = skb_shinfo(skb);
    for (i = 0; i < shinfo->nr_frags; i++) {
        tty->ops->write(tty,
                        skb_frag_address(&shinfo->frags[i]),
                        skb_frag_size(&shinfo->frags[i]));
    }
    tty->ops->write(tty, &endflag, 1);
    consume_skb(skb);
    atomic64_add(1, &uart_wifi->tx_packets);
    atomic64_add(send_len,&uart_wifi->tx_bytes);
    return NETDEV_TX_OK;
}
void uart_wifi_stats64(struct net_device *netdev,struct rtnl_link_stats64 *storage)
{
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(&netdev->dev);
    storage->tx_packets = atomic64_read(&uart_wifi->tx_packets);
    storage->tx_bytes =   atomic64_read(&uart_wifi->tx_bytes);
    storage->rx_packets = atomic64_read(&uart_wifi->rx_packets);
    storage->rx_bytes =   atomic64_read(&uart_wifi->rx_bytes);
}
static const struct net_device_ops uart_wifi_netdev_ops = {
	.ndo_open		= uart_wifi_open,
	.ndo_stop		= uart_wifi_stop,
	//.ndo_set_config		= uart_wifi_config,
	.ndo_start_xmit		= uart_wifi_xmit,
    //.ndo_set_mac_address = eth_mac_addr,
    //.ndo_validate_addr	= eth_validate_addr,
	//.ndo_do_ioctl		= uart_wifi_ioctl,
	.ndo_get_stats64		= uart_wifi_stats64,
	//.ndo_change_mtu		= uart_wifi_change_mtu,
	//.ndo_tx_timeout    = uart_wifi_tx_timeout,
};




void uart_wifi_receive(struct net_device* netdev ,const char *buf, int len)
{
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(&netdev->dev);
    struct sk_buff *skb = netdev_alloc_skb(netdev, ETH_HLEN+len);
    skb_copy_to_linear_data_offset(skb,ETH_HLEN,buf,len);
    skb->protocol = htons(ETH_P_IP);//big endian
    ((struct ethhdr *)skb->data)->h_proto = skb->protocol;
    netif_receive_skb(skb);
    atomic64_add(1, &uart_wifi->rx_packets);
    atomic64_add(len,&uart_wifi->rx_bytes);
}
                

int netdev_init(struct uart_wifi_struct *uart_wifi)
{
    struct net_device *netdev;
    struct device *dev=&uart_wifi->pdev->dev;

    atomic64_set(&uart_wifi->rx_packets,0);
    atomic64_set(&uart_wifi->rx_bytes,0);
    atomic64_set(&uart_wifi->tx_packets,0);
    atomic64_set(&uart_wifi->tx_bytes,0);
    netdev = devm_alloc_etherdev(dev, 0);
    if(!netdev) {
        dev_err(dev,"%s: failed to create netdev\n",__func__);
        return -1;
    }
    netdev->netdev_ops = &uart_wifi_netdev_ops;


    //netdev->hard_header_len = 0;
    //netdev->addr_len = 0;
    //netdev->type = ARPHRD_NONE;
    //netdev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
    netdev->flags = IFF_NOARP | IFF_MULTICAST;
    netdev->features  |=  NETIF_F_GRO |NETIF_F_GSO|NETIF_F_SG;
    dev_set_drvdata(&netdev->dev, uart_wifi);
    uart_wifi->netdev=netdev;
    dev_err(dev,"%p %p %p\n",netdev,netdev->netdev_ops,&uart_wifi_netdev_ops);
    if(register_netdev(netdev)) {
        dev_err(dev,"fail to register netdev\n");
        return -1;
    }
    uart_wifi->netdev=netdev;
    return 0;
}