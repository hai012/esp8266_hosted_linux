

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "lwip/raw.h"
#include "lwip/ip.h"
#include "lwip/icmp.h"
#include "lwip/inet.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp8266/uart_struct.h"
#include <string.h>
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include "esp_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include <stdlib.h>
#include <string.h>



#define SPLIT_DATA 0x55
#define SPLIT_CMD 0xaa
#define MTU_LEN 1500
#define MAX_FRAME_LEN (2+MTU_LEN+1)//[2byte data_len][n byte data][1byte split]
//#define UART_FIFO_LEN 128

#define STATUS_LEN 0
#define STATUS_DATA 1
#define STATUS_SPLIT 2


#define TAG "UART_WIFI"





int queue_init();
void dump(const char *tag,void *ptr, int len);

void wifi2uart();

void recv_ssid(uint8_t *output);
void recv_passwd(uint8_t *output);
void uart2wifi();
void read_uart0();

void send_to_uart0_sync(void *uartSendPtr,uint16_t uartSendLen);
void recv_netif_info(void *netif_info,uint16_t total_len);