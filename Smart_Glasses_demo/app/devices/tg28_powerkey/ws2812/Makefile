

#obj-m += key_custom.o  # 替换为你的驱动模块名（如led.o）
# 交叉编译工具链+架构（根据你的实际工具链修改）
#CROSS_COMPILE := arm-rockchip830-linux-uclibcgnueabihf-  # 示例：ARM32工具链（根据实际改）
#ARCH := arm                           # 示例：ARM32架构（arm64/mips等）
#KERNELDIR := /home/yuanwang/learn-git/Smart_Glasses_SDK/rv1106-sdk/sysdrv/source/kernel  # 替换为目标板内核源码绝对路径
#PWD := $(shell pwd)

#all:
#	make -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules
#clean:
#	make -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean
# 驱动模块名（仅改这1处）

MODULE_NAME := leds-ws2812-pwm

# 基础配置（无需修改）
SHELL := /bin/bash
KERNEL_SRC  := /home/yuanwang/learn-git/Smart_Glasses_SDK/rv1106-sdk/sysdrv/source/kernel
KERNEL_OBJ  := /home/yuanwang/learn-git/Smart_Glasses_SDK/rv1106-sdk/sysdrv/source/objs_kernel
CROSS_COMPILE := arm-rockchip830-linux-uclibcgnueabihf-  # 已确认工具链在PATH中
ARCH        := arm
PWD         := $(shell pwd)

# 内核模块编译规则
obj-m += $(MODULE_NAME).o
# 关键：添加EXTRA_CFLAGS关闭C90语法警告
EXTRA_CFLAGS += -Wno-declaration-after-statement

# 编译目标
all:
	@echo "验证内核配置文件..."
	@if [ ! -f "$(KERNEL_OBJ)/.config" ]; then \
		echo "错误：找不到内核配置文件！路径：$(KERNEL_OBJ)/.config"; \
		exit 1; \
	fi
	@echo "开始编译RV1106驱动模块: $(MODULE_NAME)"
	make -C $(KERNEL_SRC) O=$(KERNEL_OBJ) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) modules

# 清理目标
clean:
	make -C $(KERNEL_SRC) O=$(KERNEL_OBJ) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(PWD) clean
	rm -rf .*.cmd *.mod.c *.o *.ko .tmp_versions modules.order Module.symvers