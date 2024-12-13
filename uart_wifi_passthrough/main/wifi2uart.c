
#include "common.h"


//main 函数执行到主循环时heap_size=63208
#define MAX_QUEUE_LENGTH 1024*32  // 队列最大元素总长度
#define QUEUE_OK            0
#define QUEUE_FULL          1
#define QUEUE_ERROR         2

typedef struct QueueElement {
    void *data;           // 元素数据
    uint16_t length;        // 元素长度
    struct QueueElement *next;  // 下一个元素的指针
} QueueElement;

typedef struct {
    QueueElement *head;        // 队列头
    QueueElement *tail;        // 队列尾
    uint16_t max_total_length;   // 最大队列总长度
    uint16_t current_total_length; // 当前队列总长度
    SemaphoreHandle_t mutex;   // 互斥锁
} ThreadSafeQueue;



ThreadSafeQueue* eth_recv_queue;


// 初始化队列
int queue_init() {
    ThreadSafeQueue *queue = (ThreadSafeQueue*) pvPortMalloc(sizeof(ThreadSafeQueue));
    if (queue == NULL) {
        return -1; // 内存分配失败
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->max_total_length = MAX_QUEUE_LENGTH;
    queue->current_total_length = 0;
    queue->mutex = xSemaphoreCreateMutex();

    if (queue->mutex == NULL) {
        vPortFree(queue);
        return -1; // 创建互斥锁失败
    }
    eth_recv_queue = queue;
    return 0;
}

// 入队操作
int queue_enqueue(ThreadSafeQueue *queue, void *data, uint16_t length) {
    if (queue == NULL || data == NULL || length == 0) {
        return QUEUE_ERROR; // 非法参数
    }

    // 获取互斥锁
    if (xSemaphoreTake(queue->mutex, portMAX_DELAY) != pdTRUE) {
        return QUEUE_ERROR; // 获取锁失败
    }

    // 检查队列总长度是否已超过限制
    if (queue->current_total_length + length > queue->max_total_length) {
        xSemaphoreGive(queue->mutex);
        return QUEUE_FULL; // 队列已满
    }

    // 创建新元素
    QueueElement *new_element = (QueueElement*) pvPortMalloc(sizeof(QueueElement));
    if (new_element == NULL) {
        xSemaphoreGive(queue->mutex);
        return QUEUE_ERROR; // 内存分配失败
    }

    new_element->data = pvPortMalloc(length);
    if (new_element->data == NULL) {
        vPortFree(new_element);
        xSemaphoreGive(queue->mutex);
        return QUEUE_ERROR; // 内存分配失败
    }

    // 复制数据到新元素
    memcpy(new_element->data, data, length);
    new_element->length = length;
    new_element->next = NULL;

    // 将新元素加入队列
    if (queue->tail == NULL) {
        queue->head = new_element;
        queue->tail = new_element;
    } else {
        queue->tail->next = new_element;
        queue->tail = new_element;
    }

    // 更新队列总长度
    queue->current_total_length += length;

    // 释放互斥锁
    xSemaphoreGive(queue->mutex);

    return QUEUE_OK;
}

// 出队操作
int queue_dequeue(ThreadSafeQueue *queue, void **data, uint16_t *length) {
    if (queue == NULL || data == NULL || length == NULL) {
        return QUEUE_ERROR; // 非法参数
    }

    // 获取互斥锁
    if (xSemaphoreTake(queue->mutex, portMAX_DELAY) != pdTRUE) {
        return QUEUE_ERROR; // 获取锁失败
    }

    if (queue->head == NULL) {
        xSemaphoreGive(queue->mutex);
        return QUEUE_ERROR; // 队列为空
    }

    // 获取队列头部元素
    QueueElement *first_element = queue->head;
    *data = first_element->data;
    *length = first_element->length;

    // 更新队列头部
    queue->head = first_element->next;
    if (queue->head == NULL) {
        queue->tail = NULL; // 如果队列为空，尾部也需要置空
    }

    // 更新队列总长度
    queue->current_total_length -= first_element->length;

    // 释放队列头部元素内存
    vPortFree(first_element);

    // 释放互斥锁
    xSemaphoreGive(queue->mutex);

    return QUEUE_OK;
}

// 清空队列
void queue_clear(ThreadSafeQueue *queue) {
    if (queue == NULL) {
        return;
    }

    // 获取互斥锁
    if (xSemaphoreTake(queue->mutex, portMAX_DELAY) != pdTRUE) {
        return; // 获取锁失败
    }

    QueueElement *current = queue->head;
    while (current != NULL) {
        QueueElement *temp = current;
        current = current->next;
        vPortFree(temp->data);
        vPortFree(temp);
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->current_total_length = 0;

    // 释放互斥锁
    xSemaphoreGive(queue->mutex);
}

// 销毁队列
void queue_destroy(ThreadSafeQueue *queue) {
    if (queue == NULL) {
        return;
    }

    // 清空队列
    queue_clear(queue);

    // 删除互斥锁
    vSemaphoreDelete(queue->mutex);

    // 释放队列内存
    vPortFree(queue);
}


static char output[MAX_FRAME_LEN*5+1];
void wifi_recv_dump(const char *tag,void *ptr, int len)
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
    ESP_LOGI(__func__, "%s:%s",tag,output);
}



void uart_wifi_recv(struct pbuf *p)//called in ip4_input func
{/*pbuf 没有分片，可参考/ESP8266_RTOS_SDK/components/tcpip_adapter/tcpip_adapter_lwip.c
   该文件中的tcpip_adapter_recv_cb 负责pbuf创建并初始化并丢lwip协议栈，调用链如下:
   tcpip_adapter_recv_cb
           |__________pbuf_alloced_custom
           |__________ethernetif_input
                            |__________netif->input/ethernet_input
                                                   |__________ip4_input
                                                                  |_____uart_wifi_recv
   一般netif_add注册网卡时将netif->input设置成通过netif_add参数传入的ethernet_input函数
   */

    if (queue_enqueue(eth_recv_queue, p->payload, p->len) != QUEUE_OK) {
        ESP_LOGI(__func__,"Queue is full, fail to enqueue data");
    }
}

static DRAM_ATTR uart_dev_t *uart_reg = &uart0;
static inline uint16_t uart0_fill_txfifo(const char *buffer, uint16_t len)
{
    uint8_t i = 0;
    uint8_t tx_fifo_cnt = uart_reg->status.txfifo_cnt;
    uint8_t tx_remain_fifo_cnt = (UART_FIFO_LEN - tx_fifo_cnt);
    uint8_t copy_cnt = (len >= tx_remain_fifo_cnt ? tx_remain_fifo_cnt : len);
    for (i = 0; i < copy_cnt; i++) {
        uart_reg->fifo.rw_byte = buffer[i];
    }
    return copy_cnt;
}



uint16_t uartSendLen=0;
char uartSendbuffer[MAX_FRAME_LEN]; 
char *uartSendPtr=uartSendbuffer;
void wifi2uart() {
    uint16_t ret;
    void *data;
    if (uartSendLen <= 0) {
        if (queue_dequeue(eth_recv_queue, &data , &uartSendLen) == QUEUE_OK) {
            //wifi_recv_dump("wifi_recv",data, uartSendLen);
            memcpy(uartSendbuffer+2, data, uartSendLen);
            vPortFree(data);
            // if(check_ip_checksum()){
            //     ESP_LOGI(__func__,"recv error,ret=%hd:%s", ret,strerror(errno));
            //     wifi_recv_dump("recv checksum error:",uartSendbuffer+2,ret);
            //     return;
            // };
            *((uint16_t *)uartSendbuffer) = uartSendLen;
            uartSendLen += 3;
            uartSendbuffer[uartSendLen-1] = SPLIT_DATA;
            uartSendPtr = uartSendbuffer;
        }
    }
    if (uartSendLen > 0) {
        ret = uart0_fill_txfifo(uartSendPtr, uartSendLen);
        // if(!ret) {
        //     ESP_LOGI(__func__,"uart0_fill_txfifo,ret=0");
        // }
        uartSendLen -= ret;
        uartSendPtr += ret;
    }
}


void send_to_uart0_sync(void *uartSendPtr,uint16_t uartSendLen)
{
    uint16_t ret;
    while(uartSendLen) {
        ret = uart0_fill_txfifo(uartSendPtr, uartSendLen);
        uartSendLen -= ret;
        uartSendPtr += ret;
    }
}