ifneq ($(KERNELRELEASE),)
#	obj-m := sysfs.o gps_quadrino.o
	obj-m := gps_quadrino.o
	gps_quadrino-objs := gps-quadrino.o nmea.o
else
	MODULE_NAME=gps_quadrino
	SOURCES=gps-quadrino.c nmea.c
	OBJECTS+=$(addsuffix .o,$(basename $(SOURCES)))
	KERNEL_DIR ?= $(HOME)/src/linux
	MODULE_INSTALL_PATH?=/lib/modules/`uname -r`/kernel/drivers/gps
	BOOT_PATH=$(wildcard /flash /boot)

all: module dtoverlay

module: $(MODULE_NAME).ko
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD

clean:
	rm *.o *.dtb*

dtoverlay: $(MODULE_NAME).dtbo

dtoverlay_install: dtoverlay
	cp $(MODULE_NAME).dtbo $(BOOT_PATH)/overlays

install: dtoverlay_install
	mkdir -p $(MODULE_INSTALL_PATH)
	cp $(MODULE_NAME).ko $(MODULE_INSTALL_PATH)
	depmod -a


install_link: dtoverlay_install
	mkdir -p $(MODULE_INSTALL_PATH)
	ln -s `pwd`/$(MODULE_NAME).ko $(MODULE_INSTALL_PATH)/$(MODULE_NAME).ko
	depmod -a

test:
	gcc -I/usr/include -I/usr/local/include test.c -o test && ./test

$(MODULE_NAME).dtbo: $(MODULE_NAME).dts
	dtc -@ -I dts -O dtb -o $(MODULE_NAME).dtbo $(MODULE_NAME).dts


endif

