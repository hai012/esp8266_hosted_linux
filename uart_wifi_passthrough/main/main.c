

#include "common.h"


char runFlag=0;
SemaphoreHandle_t uart_wifi_mutex;






extern struct netif *netif_list;
static struct netif * netif;
struct netif *target_wifi_netif = NULL;
char netif_info[15];
void start_hock(tcpip_adapter_ip_info_t *ip_info_ptr){
    ip4_addr_t ip = ip_info_ptr->ip;
    ip4_addr_t netmask = ip_info_ptr->netmask;
    ip4_addr_t gw = ip_info_ptr->gw;
    ESP_LOGI(__func__, "IP_EVENT_STA_GOT_IP:IP:"IPSTR, IP2STR(&ip));
    ESP_LOGI(__func__, "IP_EVENT_STA_GOT_IP:MASK:"IPSTR, IP2STR(&netmask));
    ESP_LOGI(__func__, "IP_EVENT_STA_GOT_IP:GW:"IPSTR, IP2STR(&gw));

    struct netif * netif= netif_list;
    while (netif != NULL) {
        if(netif->ip_addr.addr == ip.addr) {
            ESP_LOGI(__func__, "Netif name: %c%c%d", netif->name[0], netif->name[1],(int)netif->num);
            ESP_LOGI(__func__, "Netif mtu: %d", (int)netif->mtu);
            ESP_LOGI(__func__, "Netif IP:"IPSTR, IP2STR(&netif->ip_addr));
            ESP_LOGI(__func__, "Netif MASK:"IPSTR, IP2STR(&netif->netmask));
            ESP_LOGI(__func__, "Netif GW:"IPSTR, IP2STR(&netif->gw));
            *((uint16_t *)netif_info) = 12;
            memcpy(netif_info+2,&netif->ip_addr,4);
            memcpy(netif_info+6,&netif->netmask,4);
            memcpy(netif_info+10,&netif->gw,4);
            netif_info[14]=SPLIT_CMD;
            break;
        }
        netif = netif->next;
    }
    if(netif == NULL) {
        ESP_LOGI(__func__, "fail to match netif");
        return;
    }

    send_to_uart0_sync(netif_info,sizeof(netif_info));
    recv_netif_info(netif_info,sizeof(netif_info));

    target_wifi_netif = netif;//hock ip4_input in ESP8266_RTOS_SDK/components/lwip/lwip/src/core/ipv4/ip4.c

    gpio_set_level(GPIO_NUM_5, 0);
    runFlag=1;
    xSemaphoreGive(uart_wifi_mutex);

}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                ESP_LOGI(__func__, "Wi-Fi Station mode started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(__func__, "Connected to Wi-Fi network");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(__func__, "Disconnected from Wi-Fi network");
                runFlag=0;
                break;
            case WIFI_EVENT_STA_AUTHMODE_CHANGE:
                ESP_LOGI(__func__, "Wi-Fi authentication mode changed");
                break;
            default:
                ESP_LOGI(__func__, "Unhandled Wi-Fi event %d", event_id);
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            start_hock(&event->ip_info);
        }
    }
}




void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));



    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    //Initialize Wi-Fi
#if 1
    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    ESP_LOGI(__func__, "start recv_ssid.");
    recv_ssid(wifi_config.sta.ssid);
    ESP_LOGI(__func__, "start recv_passwd.");
    recv_passwd(wifi_config.sta.password);
#else
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "CMCC-gxf",
            .password = "15874414321",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
#endif
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));  //设置wifi模式为sta
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));    //设置wifi的配置参数
    ESP_ERROR_CHECK(esp_wifi_start());  //使用当前配置启动wifi工作
}

void init()
{
    uart_wifi_mutex = xSemaphoreCreateBinary();
    if (uart_wifi_mutex == NULL) {
        // 信号量创建失败，进行错误处理
        ESP_LOGI(__func__, "Failed to create uart_wifi_mutex");
        return;
    }
    if(queue_init()) {
        ESP_LOGI(__func__, "fail to create uart_wifi_xQueue");
        return;
    }

    ESP_LOGI(__func__, "start GPIO INIT");
    // 配置 GPIO5 为输出模式
    gpio_config_t io_conf;
    io_conf.pin_bit_mask = ((1ULL<<GPIO_NUM_5));  // 选择 GPIO5
    io_conf.mode = GPIO_MODE_OUTPUT;              // 设置为输出模式
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;     // 不启用上拉
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE; // 不启用下拉
    gpio_config(&io_conf);                        // 配置 GPIO
    gpio_set_level(GPIO_NUM_5, 1);                // 设置为高电平

    //Initialize uart0
    ESP_LOGI(__func__, "start uart0 init");
    uart_enable_swap();
    uart_disable_intr_mask(UART_NUM_0, UART_INTR_MASK);//disable all uart0 irq
    gpio_pullup_en(GPIO_NUM_1);//rts
    gpio_pullup_en(GPIO_NUM_3);//cts
    gpio_pullup_en(GPIO_NUM_13);//rx
    gpio_pullup_en(GPIO_NUM_15);//tx

    uart_config_t uart_config = {
        .baud_rate = 921600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS,
        .rx_flow_ctrl_thresh = 126
    };
    uart_param_config(UART_NUM_0, &uart_config);

    ESP_LOGI(__func__, "start wifi INIT");
    wifi_init_sta();

    ESP_LOGI(__func__, "start wait uart_wifi_mutex");
    xSemaphoreTake(uart_wifi_mutex, portMAX_DELAY);//pdMS_TO_TICKS(1000)
}



// 主函数
void app_main() {
    ESP_LOGI("Wi-Fi", "start app_main");
    init();
    ESP_LOGI("Wi-Fi", "init OK, heap_size=%u",esp_get_free_heap_size());
    
    //send_test();
    while(runFlag) {
        uart2wifi();
        wifi2uart();
    }
    gpio_set_level(GPIO_NUM_5, 1);//notify_link_down

    while(1){//dummy read to clear host tx fifo
        read_uart0();
    }
}