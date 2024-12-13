#include "common.h"


static ssize_t str_show_connect_info(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(dev);
    if (wait_event_interruptible(uart_wifi->netif_info_wait_queue, uart_wifi->netif_info_vaild)) {
        return 0;  // 被信号中断时返回
    }
    memcpy(buf,uart_wifi->netif_info+15, 12*4);
    return 12*4;
}



static ssize_t str_show(struct device *dev, struct device_attribute *attr, char *buf)
{   //buf好像默认是PAGE_SIZE大小吗？
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(dev);
    ssize_t ret=-EINVAL;
    
    if(!strcmp(attr->attr.name,"tty_name")) {
        read_lock(uart_wifi->rwlock_attr);
        ret=snprintf(buf, sizeof(uart_wifi->tty_name),"%s\n",uart_wifi->tty_name);
        read_unlock(uart_wifi->rwlock_attr);
    } else if(!strcmp(attr->attr.name,"ssid")) {
        read_lock(uart_wifi->rwlock_attr);
        ret= snprintf(buf, WPA_MAX_SSID_LEN,"%s\n",uart_wifi->ssid+2);
        read_unlock(uart_wifi->rwlock_attr);
    } else if(!strcmp(attr->attr.name,"password")) {
        read_lock(uart_wifi->rwlock_attr);
        ret= snprintf(buf, WPA_MAX_PASSWD_LEN,"%s\n",uart_wifi->password+2);
        read_unlock(uart_wifi->rwlock_attr);
    }
    return ret;
}

// 写入属性函数
static ssize_t str_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    // 执行 echo "abc" > node 时 buf 将是 "abc\n"，count 为 4
    struct uart_wifi_struct* uart_wifi = dev_get_drvdata(dev);

    if(buf[count-1] != '\n'){
        return -EINVAL;
    }

    if(!strcmp(attr->attr.name,"tty_name")) {
        if (count > sizeof(uart_wifi->tty_name)) {
            return -EINVAL;
        }
        write_lock(uart_wifi->rwlock_attr);
        strncpy(uart_wifi->tty_name, buf, count);
        uart_wifi->tty_name[count-1]='\0';
        write_unlock(uart_wifi->rwlock_attr);
        return count;
    }
    if(!strcmp(attr->attr.name,"ssid")) {
        if (count > WPA_MAX_SSID_LEN) {
            return -EINVAL;
        }
        write_lock(uart_wifi->rwlock_attr);
        *((uint16_t *)uart_wifi->ssid) = count;
        strncpy(uart_wifi->ssid+2, buf, count);
        uart_wifi->ssid[2+count-1]='\0';
        *(uart_wifi->ssid + 2 + count) = SPLIT_CMD;
        write_unlock(uart_wifi->rwlock_attr);
        return count;
    }
    if(!strcmp(attr->attr.name,"password")) {
        if (count > WPA_MAX_PASSWD_LEN) {
            return -EINVAL;
        }
        write_lock(uart_wifi->rwlock_attr);
        *((uint16_t *)uart_wifi->password) = count;
        strncpy(uart_wifi->password+2, buf, count);
        uart_wifi->password[2+count-1]='\0';
        *(uart_wifi->password + 2 + count) = SPLIT_CMD;
        write_unlock(uart_wifi->rwlock_attr);
        return count;
    }
    return -EINVAL;
}
static DEVICE_ATTR(tty_name, S_IRUGO | S_IWUSR, str_show, str_store);
static DEVICE_ATTR(ssid,     S_IRUGO | S_IWUSR, str_show, str_store);
static DEVICE_ATTR(password, S_IRUGO | S_IWUSR, str_show, str_store);
static DEVICE_ATTR(netif_info, S_IRUGO, str_show_connect_info, NULL);

static int uart_wifi_probe(struct platform_device *pdev)
{
    int ret;
    const char *ptr;
    int count;
    struct uart_wifi_struct *uart_wifi;
    struct device *dev=&pdev->dev;

    uart_wifi = devm_kzalloc(dev,sizeof(struct uart_wifi_struct),GFP_KERNEL);
    if (!uart_wifi)
        return -ENOMEM;

    uart_wifi->pdev= pdev;
    dev_set_drvdata(dev, uart_wifi);
    
    uart_wifi->gpio_reset = of_get_named_gpio(dev->of_node, "gpios", 0);
    ret = devm_gpio_request(dev, uart_wifi->gpio_reset, "uart_wifi gpio_reset");
    if (ret) {
        dev_err(dev,"Failed requesting gpio_reset\n");
        return ret;
    }
    ret = gpio_direction_output(uart_wifi->gpio_reset, 0);
    if (ret) {
        dev_err(dev,"Failed to set GPIO output direction for gpio_reset\n");
        return ret;
    }

    uart_wifi->gpio_intr = of_get_named_gpio(dev->of_node, "gpios", 1);
    ret = devm_gpio_request(dev, uart_wifi->gpio_intr, "uart_wifi gpio_intr");
    if (ret) {
        dev_err(dev, "Failed requesting gpio_intr\n");
        return ret;
    }
    ret = gpio_direction_input(uart_wifi->gpio_intr);
    if (ret) {
        dev_err(dev,"Failed to set GPIO input direction for gpio_intr\n");
        return ret;
    }
    uart_wifi->irq_num_gpio_intr = gpio_to_irq(uart_wifi->gpio_intr);
    if (uart_wifi->irq_num_gpio_intr < 0) {
        dev_err(dev,"Failed to get IRQ number for irq_num_gpio_intr: %d\n", uart_wifi->irq_num_gpio_intr);
        return uart_wifi->irq_num_gpio_intr;
    }

    ret = of_property_read_string(dev->of_node, "tty_name", &ptr);
    if (ret) {
        dev_info(dev, "Failed to read 'tty_name' from device tree\n");
        uart_wifi->tty_name[0] = '\0';
    } else {
        count = strlen(ptr) + 1;
        if(count > sizeof(uart_wifi->tty_name)) {
            dev_info(dev, "Failed to read 'tty_name' from device tree\n");
            uart_wifi->tty_name[0] = '\0';
        } else {
            memcpy(uart_wifi->tty_name,ptr,count);
        }
    }
    
    ret = of_property_read_string(dev->of_node, "ssid", &ptr);
    if (ret) {
        dev_info(dev, "Failed to read 'ssid' from device tree\n");
        *((uint16_t *)uart_wifi->ssid) = 0;
        *(uart_wifi->ssid + 2) = '\0';
        *(uart_wifi->ssid + 2 + 1) = SPLIT_CMD;
    } else {
        count = strlen(ptr)+1;
        if(count > WPA_MAX_SSID_LEN) {
            dev_info(dev, "Failed to read 'ssid' from device tree\n");
            *((uint16_t *)uart_wifi->ssid) = 0;
            *(uart_wifi->ssid + 2) = '\0';
            *(uart_wifi->ssid + 2 + 1) = SPLIT_CMD;
        } else {
            *((uint16_t *)uart_wifi->ssid) = count;
            memcpy(uart_wifi->ssid+2,ptr,count);
            *(uart_wifi->ssid + 2 + count) = SPLIT_CMD; 
        }
    }

    ret = of_property_read_string(dev->of_node, "password", &ptr);
    if (ret) {
        dev_info(dev, "Failed to read 'password' from device tree\n");
        *((uint16_t *)uart_wifi->password) = 0;
        *(uart_wifi->password + 2) = '\0';
        *(uart_wifi->password + 2 + 1) = SPLIT_CMD;
    } else {
        count = strlen(ptr) + 1;
        if(count > sizeof(uart_wifi->password)) {
            dev_info(dev, "Failed to read 'password' from device tree\n");
            *((uint16_t *)uart_wifi->password) = 0;
            *(uart_wifi->password + 2) = '\0';
            *(uart_wifi->password + 2 + 1) = SPLIT_CMD;
        } else {
            *((uint16_t *)uart_wifi->password) = count;
            memcpy(uart_wifi->password+2,ptr,count);
            *(uart_wifi->password + 2 + count) = SPLIT_CMD; 
        }
    }

    ret = device_create_file(dev, &dev_attr_tty_name);
    if (ret) {
        dev_err(dev,"Failed to create sysfs entry for tty_name\n");
        return ret;
    }
    ret = device_create_file(dev, &dev_attr_ssid);
    if (ret) {
        dev_err(dev,"Failed to create sysfs entry for ssid\n");
        return ret;
    }
    ret = device_create_file(dev, &dev_attr_password);
    if (ret) {
        dev_err(dev,"Failed to create sysfs entry for password\n");
        return ret;
    }

    uart_wifi->netif_info_vaild = 0;
    init_waitqueue_head(&uart_wifi->netif_info_wait_queue);
    ret = device_create_file(dev, &dev_attr_netif_info);
    if (ret) {
        dev_err(dev,"Failed to create sysfs entry for password\n");
        return ret;
    }

    if(netdev_init(uart_wifi)) {
        dev_err(dev,"netdev_init is failed\n");
        return ret;
    }
    return 0;
}


static int uart_wifi_remove(struct platform_device *pdev)
{
    struct uart_wifi_struct *uart_wifi = dev_get_drvdata(&pdev->dev);
    struct device *dev=&pdev->dev;
    dev_err(dev,"uart_wifi_remove\n");
    unregister_netdev(uart_wifi->netdev);
    device_remove_file(dev, &dev_attr_tty_name);
    device_remove_file(dev, &dev_attr_ssid);
    device_remove_file(dev, &dev_attr_password);
    return 0;
}
static const struct of_device_id id_table[] = {
	{.compatible = "ghj,uart_wifi",},
	{},
};
static struct platform_driver uart_wifi_drv = {
	.probe	=	uart_wifi_probe,
	.remove	=	uart_wifi_remove,
	.driver	=	{
		.name	=	"uart_wifi",
		.of_match_table	= id_table,	
	}
};
static int __init uart_wifi_drv_init(void)
{
    int ret = tty_register_ldisc(N_DEVELOPMENT, &ttyio_ldisc_ops);
    if (ret) {
        pr_err("uart_wifi:Failed to register ldisc\n");
        return ret;
    }
    return platform_driver_register(&uart_wifi_drv);
}
module_init(uart_wifi_drv_init);
static void __exit uart_wifi_drv_exit(void) 
{
    int ret;
    platform_driver_unregister(&uart_wifi_drv);
    ret = tty_unregister_ldisc(N_DEVELOPMENT);
    if (ret) {
        pr_err("uart_wifi:Failed to unregister ldisc\n");
    }
}
module_exit(uart_wifi_drv_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Haijie Gong");
MODULE_DESCRIPTION("uart_wifi module");
