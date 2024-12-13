


# 设置 Linux 内核源码路径
KERNEL_DIR := ../linux
# 配置内核构建目录和目标
KBUILD_OUTPUT := $(KERNEL_DIR)/build


# 当前目录
PWD := $(shell pwd)

# 模块名称
MODULE_NAME := uart_wifi

# 编译模块的源文件

obj-m := $(MODULE_NAME).o
$(MODULE_NAME)-objs := main.o netdev.o uart.o dump.o

.PHONY: all build install clean

# 默认目标
all:build install

build:
	$(MAKE) -C $(KERNEL_DIR) O=$(KBUILD_OUTPUT) M=$(PWD) modules

install:
	adb push ./run.sh /tmp
	adb push ./uart_wifi.ko  /tmp
	adb shell /tmp/run.sh



clean:
	$(MAKE) -C $(KERNEL_DIR) O=$(KBUILD_OUTPUT) M=$(PWD) clean
	
	