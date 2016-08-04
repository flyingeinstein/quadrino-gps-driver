ifneq ($(KERNELRELEASE),)
#	obj-m := sysfs.o gps-quadrino.o
	obj-m := gps-quadrino.o
else
	MODULE_NAME=gps-quadrino
	KERNEL_DIR ?= $(HOME)/src/linux
	MODULE_INSTALL_PATH?=/lib/modules/`uname -r`/kernel/drivers/gps
all:
		$(MAKE) -C $(KERNEL_DIR) M=$$PWD

clean:
	    rm *.o 

install:
	mkdir -p $(MODULE_INSTALL_PATH)
	cp $(MODULE_NAME).ko $(MODULE_INSTALL_PATH)
	depmod -a


install_link:
	mkdir -p $(MODULE_INSTALL_PATH)
	ln -s `pwd`/$(MODULE_NAME).ko $(MODULE_INSTALL_PATH)/$(MODULE_NAME).ko
	depmod -a

test:
	gcc -I/usr/include -I/usr/local/include test.c -o test && ./test
endif
