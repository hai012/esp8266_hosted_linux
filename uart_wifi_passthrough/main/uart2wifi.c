#include "common.h"



static char output[MAX_FRAME_LEN*5+1];
void wifi_send_dump(const char *tag,void *ptr, int len)
{
    int i;
    int offset = 0;
    char *buf=ptr;
    for (i = 0; i < len; i++) {
        if (offset >= sizeof(output)-5) {
            break;
        }
        offset += snprintf(output + offset, sizeof(output) - offset, "0x%02X,", buf[i]);
    }
    output[offset]='\0';
    ESP_LOGI(tag, "%s:%s",tag,output);
}






char status = STATUS_LEN;

static DRAM_ATTR uart_dev_t *uart_reg = &uart0;
int16_t uartRecvLen=0;
int16_t uartDataLen=0;
uint8_t uartRecvBuffer[MAX_FRAME_LEN+UART_FIFO_LEN]; 

static inline int uart0_recv_data(uint8_t *buf) {
    int16_t fifo_data_cnt = uart_reg->status.rxfifo_cnt;
    int16_t fifo_read_cnt=0;
    while (fifo_read_cnt < fifo_data_cnt) {
        buf[fifo_read_cnt++] = uart_reg->fifo.rw_byte;
    }
    return fifo_data_cnt;
}

void read_uart0()
{
    uart0_recv_data(uartRecvBuffer);
}


#define WPA_MAX_SSID_LEN 32
#define WPA_MIN_SSID_LEN 1
void recv_ssid(uint8_t *output) {
    int16_t ret;
    int16_t nextFrameByte;

restart:
    switch (status) {
        case STATUS_LEN:
            if (uartRecvLen < 2) {
                ret = uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            uartDataLen = *((int16_t *)uartRecvBuffer);//frame data len
            if ( uartDataLen > WPA_MAX_SSID_LEN || uartDataLen < WPA_MIN_SSID_LEN) {
                ESP_LOGI(__func__, "uartDataLen error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            status=STATUS_DATA;
            //continue;
        case STATUS_DATA:
            if (uartRecvLen < uartDataLen+3) {
                ret =  uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            if(uartRecvBuffer[uartDataLen+1]!='\0' && uartRecvBuffer[uartDataLen+2] != SPLIT_CMD) {
                ESP_LOGI(__func__, "STATUS_DATA SPLIT error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            memcpy(output,uartRecvBuffer+2,uartDataLen);
            ESP_LOGI(__func__, "recv SSID:%s",output);
            nextFrameByte=uartRecvLen-(uartDataLen+3);
            if (nextFrameByte>0)
                memmove(uartRecvBuffer,uartRecvBuffer+uartDataLen+3,nextFrameByte);
            uartRecvLen=nextFrameByte;
            status=STATUS_LEN;
            return;//处理完毕
        case STATUS_SPLIT:
            if (uartRecvLen == 0 ) {
                ret =  uart0_recv_data(uartRecvBuffer);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
            }
            // relocate split flag
            for(int i=0;i<uartRecvLen;) { 
                if (uartRecvBuffer[i++]==SPLIT_CMD) {
                    // 到这里i是SPLIT_CMD下一个字节的索引，也是当前frame长度
                    memmove(uartRecvBuffer,uartRecvBuffer+i,uartRecvLen-i);
                    uartRecvLen -= i;
                    status=STATUS_LEN;
                    goto restart;
                }
            }
            uartRecvLen = 0;
            goto restart;
    }

}



#define WPA_MAX_PASSWD_LEN 64
#define WPA_MIN_PASSWD_LEN 8
void recv_passwd(uint8_t *output) {
    int16_t ret;
    int16_t nextFrameByte;

restart:
    switch (status) {
        case STATUS_LEN:
            if (uartRecvLen < 2) {
                ret = uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            uartDataLen = *((int16_t *)uartRecvBuffer);//frame data len
            if ( uartDataLen > WPA_MAX_PASSWD_LEN || uartDataLen < WPA_MIN_PASSWD_LEN) {
                ESP_LOGI(__func__, "uartDataLen error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            status=STATUS_DATA;
            //continue;
        case STATUS_DATA:
            if (uartRecvLen < uartDataLen+3) {
                ret =  uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            if(uartRecvBuffer[uartDataLen+1]!='\0' && uartRecvBuffer[uartDataLen+2] != SPLIT_CMD) {
                ESP_LOGI(__func__, "STATUS_DATA SPLIT error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            memcpy(output,uartRecvBuffer+2,uartDataLen);
            ESP_LOGI(__func__, "recv password:%s",output);
            nextFrameByte=uartRecvLen-(uartDataLen+3);
            if (nextFrameByte>0)
                memmove(uartRecvBuffer,uartRecvBuffer+uartDataLen+3,nextFrameByte);
            uartRecvLen=nextFrameByte;
            status=STATUS_LEN;
            return;//处理完毕
        case STATUS_SPLIT:
            if (uartRecvLen == 0 ) {
                ret =  uart0_recv_data(uartRecvBuffer);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
            }
            // relocate split flag
            for(int i=0;i<uartRecvLen;) { 
                if (uartRecvBuffer[i++]==SPLIT_CMD) {
                    // 到这里i是SPLIT_CMD下一个字节的索引，也是当前frame长度
                    memmove(uartRecvBuffer,uartRecvBuffer+i,uartRecvLen-i);
                    uartRecvLen -= i;
                    status=STATUS_LEN;
                    goto restart;
                }
            }
            uartRecvLen = 0;
            goto restart;
    }
}



void recv_netif_info(void *netif_info,uint16_t total_len) {
    int16_t ret;
    int16_t nextFrameByte;

restart:
    //wifi_send_dump(__func__,uartRecvBuffer,uartRecvLen);
    switch (status) {
        case STATUS_LEN:
            if (uartRecvLen < 2) {
                ret = uart0_recv_data(uartRecvBuffer+uartRecvLen);
                ESP_LOGI(__func__, "recv:%hd",ret);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            uartDataLen = *((int16_t *)uartRecvBuffer);//frame data len
            if ( uartDataLen != *((int16_t *)netif_info)) {
                ESP_LOGI(__func__, "uartDataLen error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            status=STATUS_DATA;
            //continue;
        case STATUS_DATA:
            if (uartRecvLen < uartDataLen+3) {
                ret =  uart0_recv_data(uartRecvBuffer+uartRecvLen);
                ESP_LOGI(__func__, "recv:%hd",ret);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
                goto restart;
            }
            if(uartRecvBuffer[uartDataLen+2] != SPLIT_CMD) {
                ESP_LOGI(__func__, "STATUS_DATA SPLIT error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            if(memcmp(uartRecvBuffer,netif_info,total_len)) {
                ESP_LOGI(__func__, "netif_info match fail");
                wifi_send_dump(__func__,uartRecvBuffer,total_len);
                status=STATUS_SPLIT;
                goto restart;
            }
            ESP_LOGI(__func__, "netif_info match ok");
            nextFrameByte=uartRecvLen-(uartDataLen+3);
            if (nextFrameByte>0)
                memmove(uartRecvBuffer,uartRecvBuffer+uartDataLen+3,nextFrameByte);
            uartRecvLen=nextFrameByte;
            status=STATUS_LEN;
            return;//处理完毕
        case STATUS_SPLIT:
            if (uartRecvLen == 0 ) {
                ret =  uart0_recv_data(uartRecvBuffer);
                ESP_LOGI(__func__, "recv:%hd",ret);
                if(ret<=0) {
                    goto restart;
                }
                uartRecvLen += ret;
            }
            // relocate split flag
            for(int i=0;i<uartRecvLen;) { 
                if (uartRecvBuffer[i++]==SPLIT_CMD) {
                    // 到这里i是SPLIT_CMD下一个字节的索引，也是当前frame长度
                    memmove(uartRecvBuffer,uartRecvBuffer+i,uartRecvLen-i);
                    uartRecvLen -= i;
                    status=STATUS_LEN;
                    goto restart;
                }
            }
            uartRecvLen = 0;
            goto restart;
    }
}




extern struct netif *target_wifi_netif;

struct pbuf *send_pbuf=NULL;
struct ip4_addr *dest;
void uart2wifi() {
    int16_t ret;
    int16_t nextFrameByte;

restart:
    switch (status) {
        case STATUS_LEN:
            if (uartRecvLen < 2) {
                ret = uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    return;
                }
                //wifi_send_dump("uart_recv1",uartRecvBuffer+uartRecvLen,ret);
                uartRecvLen += ret;
                goto restart;
            }
            uartDataLen = *((int16_t *)uartRecvBuffer);//frame data len
            if ( uartDataLen > MTU_LEN || uartDataLen < 20) {
                ESP_LOGI(__func__, "uartDataLen error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            status=STATUS_DATA;
        case STATUS_DATA:
            if (uartRecvLen < uartDataLen+3) {
                ret =  uart0_recv_data(uartRecvBuffer+uartRecvLen);
                if(ret<=0) {
                    return;
                }
                //wifi_send_dump("uart_recv2",uartRecvBuffer+uartRecvLen,ret);
                uartRecvLen += ret;
                goto restart;
            }
            if(uartRecvBuffer[uartDataLen+2] != SPLIT_DATA) {
                ESP_LOGI(__func__, "STATUS_DATA SPLIT error:%hd",uartDataLen);
                status=STATUS_SPLIT;
                goto restart;
            }
            //struct ip_hdr *iphdr = (struct ip_hdr *)(uartRecvBuffer+2);
            //ip4_addr_copy(iphdr->src, ipaddr);
            //wifi_send_dump("wifi_send",uartRecvBuffer+2, uartDataLen);
            if(!send_pbuf) {
                /*这段代码参考/ESP8266_RTOS_SDK/components/lwip/lwip/src/api/sockets.c
                中lwip_sendto函数调的netbuf_alloc函数*/
                send_pbuf=pbuf_alloc(PBUF_TRANSPORT, uartDataLen, PBUF_RAM);
                if (send_pbuf == NULL) {
                    ESP_LOGI(__func__,"pbuf_alloc PBUF_TRANSPORT fail:%s",strerror(errno));
                    return;
                }
                memcpy(send_pbuf->payload,uartRecvBuffer+2,uartDataLen);
                dest = (const struct ip4_addr *) (&(((struct ip_hdr *)(send_pbuf->payload))->dest));
            }
            /*netif->linkoutput 和 netif->linkoutput的初始化见/ESP8266_RTOS_SDK/components/lwip/port/esp8266/netif/wlanif.c
            文件中的ethernetif_init 函数。 调用链如下:
            netif->output()/etharp_output
                           |_____ethernet_output
                                       |_______netif->linkoutput()/low_level_output
                                                                  |____________ieee80211_output_pbuf
            ieee80211_output_pbuf是ESP闭源固件中的，从代码推测成功发送后会回调通过参数传入的low_level_send_cb函数
            low_level_output中调用ethernetif_transform_pbuf的地方已经被我注释掉了，让后续回调
            low_level_send_cb函数调用pbuf_free(pbuf)时释放的就是一层层传下去的send_pbuf。
            */
            if (target_wifi_netif->output(target_wifi_netif, send_pbuf, dest)) {
                ESP_LOGI(__func__,"target_wifi_netif->output fail:%s",strerror(errno));// etharp_output
                return;//frame接收到了,但现在还不能立即处理
            }
            send_pbuf = NULL;
            nextFrameByte=uartRecvLen-(uartDataLen+3);
            if (nextFrameByte>0)
                memmove(uartRecvBuffer,uartRecvBuffer+uartDataLen+3,nextFrameByte);
            uartRecvLen=nextFrameByte;
            status=STATUS_LEN;
            return;//处理完毕
        case STATUS_SPLIT:
            if (uartRecvLen == 0 ) {
                ret =  uart0_recv_data(uartRecvBuffer);
                if(ret<=0) {
                    return;
                }
                wifi_send_dump("uart_recv3",uartRecvBuffer,ret);
                uartRecvLen += ret;
            }
            // relocate split flag
            for(int skip=0;skip<uartRecvLen;) { 
                if (uartRecvBuffer[skip++]==SPLIT_DATA) {
                    // 到这里i是SPLIT_DATA下一个字节的索引，也是当前frame长度
                    ESP_LOGI(__func__,"skip=%d",skip);
                    wifi_send_dump("before skip",uartRecvBuffer,uartRecvLen);
                    memmove(uartRecvBuffer,uartRecvBuffer+skip,uartRecvLen-skip);
                    uartRecvLen -= skip;
                    wifi_send_dump("after skip",uartRecvBuffer,uartRecvLen);
                    status=STATUS_LEN;
                    goto restart;
                }
            }
            uartRecvLen = 0;
            return;
    }
}

