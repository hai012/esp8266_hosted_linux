#include "common.h"







/*
串口驱动(例如imx.c)会在发送完成硬件中断或DMA传输(MEM2DEV)完成后的回调该函数中判断
如果 uart_circ_chars_pending(xmit) < WAKEUP_CHARS 则调用ttyio_write_wakeup
默认情况 serial_core.h	line 307: #define WAKEUP_CHARS 256
*/
static void ttyio_write_wakeup(struct tty_struct *tty)
{
    struct uart_wifi_struct* uart_wifi =tty->disc_data;
    struct net_device* netdev = uart_wifi->netdev;

    clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
    if(netif_carrier_ok(netdev)) {
        netif_wake_queue(netdev);//wake up queue anyway
    }
}

int alloc_new_skb_to_recv(struct uart_wifi_struct* uart_wifi)
{
    struct net_device* netdev = uart_wifi->netdev;
    struct device* dev = &uart_wifi->pdev->dev;
    uart_wifi->recv_skb = netdev_alloc_skb(netdev, MAX_RECV_LEN);
    if(!uart_wifi->recv_skb) {
        dev_err(dev, "netdev_alloc_skb is fail");
        return -1;
    }

    uart_wifi->recv_skb->dev = uart_wifi->netdev;
    skb_reset_mac_header(uart_wifi->recv_skb);
    skb_record_rx_queue(uart_wifi->recv_skb,0);
    uart_wifi->recv_skb->ip_summed = CHECKSUM_NONE;//硬件没有提供校验和需要协议栈自己去计算
    uart_wifi->recv_skb->protocol = htons(ETH_P_IP);

    //skb_reset_network_header(uart_wifi->recv_skb);
    //skb_probe_transport_header(uart_wifi->recv_skb, 0);
    //skb_reserve(uart_wifi->recv_skb,2);//for ip package 4 byte align,see ip_fast_csum
    memset(uart_wifi->recv_skb->data,0,ETH_HLEN-2);
    skb_reserve(uart_wifi->recv_skb,ETH_HLEN-2);//skip recv mac header
    uart_wifi->uart_recv_status = STATUS_LEN;
    return 0;
}



//Returns the number of bytes processed
static inline int ttyio_receive_data(struct tty_struct *tty,
                              const unsigned char *cp, //char buffer
                              char *fp, //TTY_* flags buffer
                              int count)//number of bytes need to process
{
    struct iphdr *iph;
    struct uart_wifi_struct* uart_wifi =tty->disc_data;
    struct device* dev = &uart_wifi->pdev->dev;
    int ret;
    int actualcopy,skip;
    int processed=0,surplus=count;
    struct sk_buff *skb = uart_wifi->recv_skb;
    int16_t uart_recv_status = uart_wifi->uart_recv_status;
    int16_t uartDataLen = uart_wifi->uartDataLen;
    
    //dev_err(dev, "ttyio_receive_buf2:cp=%p,count=%d",cp,count);
    //recv_dump(dev,cp,count);
    if(unlikely(!skb)) {
        if(unlikely(alloc_new_skb_to_recv(uart_wifi))){
            goto exit;
        }
        skb = uart_wifi->recv_skb;
        uart_recv_status=STATUS_LEN;
    }
restart:
    BUG_ON(skb->len<0);
    switch (uart_recv_status) {
        case STATUS_LEN:
            //dev_err(dev,"enter STATUS_LEN");
            if (skb->len < 2) {
                if(surplus==0) {
                    goto exit;
                }
                actualcopy = min(surplus, (int)(2 - skb->len));
                __skb_put_data(skb, cp + processed, actualcopy);
                surplus -= actualcopy;
                processed += actualcopy;
                goto restart;
            }
            uartDataLen = *((int16_t *)skb->data);//frame data len
            if ( uartDataLen > MTU_LEN || uartDataLen < 20 ) {
                dev_err(dev, "uartDataLen error:%hd",uartDataLen);
                uart_recv_status=STATUS_SPLIT;
                goto restart;
            }
            uart_recv_status=STATUS_DATA;
            //continue;
        case STATUS_DATA:
            //dev_err(dev,"enter STATUS_DATA");
            if (skb->len < uartDataLen+3) {
                if(surplus==0) {
                    goto exit;
                }
                actualcopy = min(surplus, (int)(uartDataLen+3 - skb->len));
                __skb_put_data(skb, cp + processed, actualcopy);
                surplus -= actualcopy;
                processed += actualcopy;
                goto restart;
            }
            if(skb->data[uartDataLen+2]==SPLIT_DATA) {
                *((uint16_t *)skb->data) = htons(ETH_P_IP);

                //recv skb
                skb->data += 2;//strip header
                skb->tail -= 1;//strip split
                skb->len  -= 3;
                //dev_err(dev,"enter recv skb");
                //recv_dump_skb(dev,skb);

                //iph = (struct iphdr *)skb->data;
                // pr_err("uart_wifi: iph=%p ",iph);
                // if (ip_fast_csum((u8 *)iph, iph->ihl)) {
                //     pr_err("uart_wifi: ip_fast_csum fail");
                // }
                //check_ip_checksum(dev,(const char *)iph, iph->ihl*4);

                //通过napi_schedule唤醒sd->backlog这个struct napi_struct ，
                //在软中断net_rx_action中回调napi poll函数,即process_backlog
                //process_backlog最终调用netif_receive_skb收包                //通过napi_schedule唤醒sd->backlog这个struct napi_struct ，
                //在软中断net_rx_action中回调napi poll函数,即process_backlog
                //process_backlog最终调用netif_receive_skb收包
                ret = netif_rx_ni(skb);
                if(ret != NET_RX_SUCCESS) {
                    dev_err(dev,"netif_rx_ni drop skb,ret=%d",ret);
                }
                atomic64_add(1, &uart_wifi->rx_packets);
                atomic64_add(skb->len,&uart_wifi->rx_bytes);
                if(unlikely(alloc_new_skb_to_recv(uart_wifi))){
                    goto exit;
                }
                skb = uart_wifi->recv_skb;
                uart_recv_status=STATUS_LEN;
                goto restart;
            }
            dev_err(dev, "STATUS_DATA SPLIT error:%hd",uartDataLen);
            uart_recv_status=STATUS_SPLIT;
        case STATUS_SPLIT:
            for(skip = 0; skip < skb->len;) {
                if(skb->data[skip++]==SPLIT_DATA) {
                    dev_err(dev,"enter STATUS_SPLIT skip=%d,len=%d,data=%p,tail=%p",skip,skb->len,skb->data,skb_tail_pointer(skb));
                    recv_dump_skb(dev,skb);
                    skb->tail -= skip;
                    skb->len -= skip;
                    memmove(skb->data, skb->data + skip, skb->len);
                    uart_recv_status=STATUS_LEN;
                    dev_err(dev,"exit STATUS_SPLIT len=%d,data=%p,tail=%p",skb->len,skb->data,skb_tail_pointer(skb));
                    recv_dump_skb(dev,skb);
                    goto restart;
                }
            }
            skb_reset_tail_pointer(skb);
            skb->len=0;
            for(skip = processed; skip < count;) {
                if(cp[skip++]==SPLIT_DATA) {
                    dev_err(dev,"enter STATUS_SPLIT cp skip_len=%d,surplus=%d,processed=%d",skip,surplus,processed);
                    dump(dev,cp,count);
                    surplus -= skip;
                    processed += skip;
                    uart_recv_status=STATUS_LEN;
                    goto restart;
                }
            }
            dev_err(dev,"enter STATUS_SPLIT skip all");
            processed = count;
    }
exit:
    uart_wifi->uart_recv_status = uart_recv_status;
    uart_wifi->uartDataLen = uartDataLen;
    //dev_err(dev, "ttyio_receive_buf2:processed=%d",processed);
    return processed;
}

void transform_netif_info(char *buf)
{
    snprintf(buf+15, 12*4, 
    "%hhu.%hhu.%hhu.%hhu\n%hhu.%hhu.%hhu.%hhu\n%hhu.%hhu.%hhu.%hhu\n", 
    buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],buf[8],buf[9],buf[10],buf[11],buf[12],buf[13]);
}

//Returns the number of bytes processed
static int ttyio_netif_info(struct tty_struct *tty,
                              const unsigned char *cp, //char buffer
                              char *fp, //TTY_* flags buffer
                              int count)//number of bytes need to process
{
    struct uart_wifi_struct* uart_wifi = tty->disc_data;
    struct device* dev = &uart_wifi->pdev->dev;
    int16_t uart_recv_status = uart_wifi->uart_recv_status;
    int16_t netif_info_recv_len = uart_wifi->netif_info_recv_len;
    int16_t uartDataLen = 12;
    int ret;
    int actualcopy,skip;
    int processed=0,surplus=count;

    dev_err(dev, "ttyio_netif_info:cp=%p,count=%d",cp,count);
    recv_dump(dev,cp,count);

restart:
    BUG_ON(netif_info_recv_len<0);

    switch (uart_recv_status) {
        case STATUS_LEN:
            dev_err(dev,"enter STATUS_LEN");
            if (netif_info_recv_len < 2) {
                if(surplus==0) {
                    goto exit;
                }
                actualcopy = min(surplus, (int)(2 - netif_info_recv_len));
                memcpy(uart_wifi->netif_info+netif_info_recv_len, cp + processed, actualcopy);
                netif_info_recv_len += actualcopy;
                surplus -= actualcopy;
                processed += actualcopy;
                goto restart;
            }
            uartDataLen = *((int16_t *)uart_wifi->netif_info);//frame data len
            if ( uartDataLen != 12 ) {
                dev_err(dev, "uartDataLen error:%hd",uartDataLen);
                if(uart_wifi->netif_info[0]==12) {
                    for(skip = 1; skip < netif_info_recv_len;skip++) {
                        if(uart_wifi->netif_info[skip]==12) {
                            dev_err(dev,"enter netif_info_recv_len=%d,skip=%d",netif_info_recv_len,skip);
                            recv_dump(dev,uart_wifi->netif_info,netif_info_recv_len);

                            netif_info_recv_len -= skip;
                            memmove(uart_wifi->netif_info, uart_wifi->netif_info + skip, netif_info_recv_len);
                            uart_recv_status=STATUS_LEN;

                            dev_err(dev,"exit netif_info_recv_len=%d,skip=%d",netif_info_recv_len,skip);
                            recv_dump(dev,uart_wifi->netif_info,netif_info_recv_len);
                            goto restart;
                        }
                    }
                }
                uart_recv_status=STATUS_SPLIT;
                goto restart;
            }
            uart_recv_status=STATUS_DATA;
            //continue;
        case STATUS_DATA:
            dev_err(dev,"enter STATUS_DATA");
            if (netif_info_recv_len < uartDataLen+3) {
                if(surplus==0) {
                    goto exit;
                }
                actualcopy = min(surplus, (int)(uartDataLen+3 - netif_info_recv_len));
                memcpy(uart_wifi->netif_info+netif_info_recv_len, cp + processed, actualcopy);
                netif_info_recv_len += actualcopy;
                surplus -= actualcopy;
                processed += actualcopy;
                goto restart;
            }
            if(uart_wifi->netif_info[uartDataLen+2]==SPLIT_CMD) {
                dev_err(dev, "get netif_info :%hhd",uartDataLen);
                recv_dump(dev,uart_wifi->netif_info,sizeof(uart_wifi->netif_info));

                tty->ops->write(tty, uart_wifi->netif_info, sizeof(uart_wifi->netif_info));

                transform_netif_info(uart_wifi->netif_info);
                uart_wifi->netif_info_vaild = 1;
                wake_up_interruptible_all(&uart_wifi->netif_info_wait_queue);

                uart_recv_status=STATUS_LEN;

                return processed;
            }
            dev_err(dev, "STATUS_DATA SPLIT error:%hd",uartDataLen);
            uart_recv_status=STATUS_SPLIT;
        case STATUS_SPLIT:
            dev_err(dev,"enter STATUS_SPLIT");
            for(skip = 0; skip < netif_info_recv_len;skip++) {
                if(uart_wifi->netif_info[skip]==12) {
                    dev_err(dev,"enter STATUS_SPLIT netif_info_recv_len=%d,skip=%d",netif_info_recv_len,skip);
                    recv_dump(dev,uart_wifi->netif_info,netif_info_recv_len);

                    netif_info_recv_len -= skip;
                    memmove(uart_wifi->netif_info, uart_wifi->netif_info + skip, netif_info_recv_len);
                    uart_recv_status=STATUS_LEN;

                    dev_err(dev,"exit STATUS_SPLIT netif_info_recv_len=%d,skip=%d",netif_info_recv_len,skip);
                    recv_dump(dev,uart_wifi->netif_info,netif_info_recv_len);
                    goto restart;
                }
            }
            netif_info_recv_len=0;
            for(skip = processed; skip < count;skip++) {
                if(cp[skip]==12) {
                    dev_err(dev,"enter STATUS_SPLIT cp skip_len=%d,surplus=%d,processed=%d",skip,surplus,processed);
                    recv_dump(dev,cp,count);
                    surplus -= skip;
                    processed += skip;
                    uart_recv_status=STATUS_LEN;
                    goto restart;
                }
            }
            dev_err(dev,"enter STATUS_SPLIT skip all");
            processed = count;
    }
exit:
    uart_wifi->uart_recv_status = uart_recv_status;
    uart_wifi->netif_info_recv_len = netif_info_recv_len;
    dev_err(dev, "processed=%d",processed);
    return processed;
}

int ttyio_receive_buf2(struct tty_struct *tty,
                              const unsigned char *cp, //char buffer
                              char *fp, //TTY_* flags buffer
                              int count)
{
    struct uart_wifi_struct* uart_wifi = tty->disc_data;
    if(unlikely(NULL==uart_wifi)) {
        /*防止 tty_set_ldisc(tty, N_DEVELOPMENT) 与 tty->disc_data=uart_wifi 之间
        串口驱动收到数据调用了ttyio_netif_info*/
        return count;
    }
    if(likely(uart_wifi->netif_info_vaild)) {
        return ttyio_receive_data(tty,cp,fp,count);
    } else {
        return ttyio_netif_info(tty,cp,fp,count);
    }
}
// int	ttyio_ldisc_open(struct tty_struct *tty)
// {
//
// }
struct tty_ldisc_ops ttyio_ldisc_ops = {
    .owner          = THIS_MODULE,
    .magic          = TTY_LDISC_MAGIC,
    .name           = "ttyio_ldisc",
    //.open           = ttyio_ldisc_open,
    // .close          = ttyio_ldisc_close,
    //不提供write使得应用程序再使用这个tty节点进行write发送时tty_write直接返回-EIO
    //提供receive_buf2 hock原厂串口驱动的接收
    .receive_buf2	= ttyio_receive_buf2,
    .write_wakeup    = ttyio_write_wakeup,//串口驱动的发送缓(不是txfifo)冲快空时被调用
};

int ttyio_init(struct uart_wifi_struct *uart_wifi)
{
    int ret;
    struct ktermios newtio;
    dev_t dev_no;
    struct device *dev = &uart_wifi->pdev->dev;
    struct tty_struct* tty;

    tty_dev_name_to_number(uart_wifi->tty_name, &dev_no);
    tty = tty_kopen(dev_no);//相当于调用了tty_open
    if (IS_ERR(tty))
        return PTR_ERR(tty);

    if (tty->ops->write && tty->ops->open)
        ret = tty->ops->open(tty, NULL);//uart_open in serial_core.c
    else
        ret = -ENODEV;
    if (ret) {
        dev_err(dev,"Failed to open %s\n",uart_wifi->tty_name);
        tty_unlock(tty);
        return ret;
    }

    clear_bit(TTY_HUPPED, &tty->flags);

    // down_read(&tty->termios_rwsem);
    // newtio = tty->termios;
    // up_read(&tty->termios_rwsem);
    memset( &newtio, 0, sizeof( newtio ) );

    //开启CREAD 开启串行数据接收
    newtio.c_cflag |= CREAD;

    //打开CLOCAL，不使用DCD载波侦测线缆
    newtio.c_cflag |= CLOCAL;

    //使能 CTS/RTS 硬件流控
    newtio.c_cflag |= CRTSCTS;

    // 配置8个数据位
    newtio.c_cflag &= ~CSIZE;
    newtio.c_cflag |= CS8;

    // 配置无奇偶校验
    newtio.c_cflag &= ~PARENB;

    // 配置1个停止位
    newtio.c_cflag &= ~CSTOPB;

    /*这些是n_tty行规程的，不需要设置*/
    newtio.c_cc[VTIME] = 0;//非规范模式读取时的超时时间；
    newtio.c_cc[VMIN]  = 0;//非规范模式读取时的最小字符数
    newtio.c_iflag &= ~(IXON | IXOFF);// 禁用软件流控制
	newtio.c_oflag  &= ~OPOST;//设置输出标志:不执行输出处理
    newtio.c_lflag  &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty_termios_encode_baud_rate(&newtio, 921600, 921600);

    tty_set_termios(tty, &newtio);

    tty_unlock(tty);

    //关闭n_tty ,吧tty设置成ttyio_ldisc ,打开ttyio_ldisc
    ret = tty_set_ldisc(tty, N_DEVELOPMENT);//设置成自定义的行规程
    if (ret) {
        dev_err(dev,"Failed to set N_DEVELOPMENT on tty\n");
        return ret;
    }
    tty->disc_data = uart_wifi;
    uart_wifi->tty = tty;
    return 0;
}




