ifneq ($(KERNELRELEASE),)
#	obj-m := sysfs.o gps_quadrino.o
	obj-m := gps_quadrino.o
	gps_quadrino-objs := gps-quadrino.o nmea.o
else
MODULE_NAME=gps_quadrino
SOURCES=gps-quadrino.c nmea.c
OBJECTS+=$(addsuffix .o,$(basename $(SOURCES)))
UNAME:= $(shell uname -r)
KERNEL_DIR?=$(dir $(wildcard $(HOME)/src/linux/Kconfig /usr/src/linux-headers-$(UNAME)/Kconfig))
MODULE_INSTALL_PATH?=/lib/modules/`uname -r`/kernel/drivers/gps
BOOT_PATH=$(wildcard /flash /boot)
DT_CONFIG=$(wildcard $(BOOT_PATH)/config.txt $(BOOT_PATH)/uEnv.txt)

BB=$(wildcard /lib/firmware/BB-I2C2*)
ifeq ($(BB),)
# RPi or generic
BOARD=rpi
else
# BeagleBoard
BOARD=bbb
endif

ifeq ($(KERNEL_DIR),)
$(error KERNEL_DIR not set and could not find linux source in ~/src/linux or headers in /usr/src/linux-headers-$(UNAME))
endif

all: module dtoverlay

info: 
	@echo BOARD=$(BOARD)

module: $(MODULE_NAME).ko
	echo $(KERNEL_DIR)
	$(MAKE) -C $(KERNEL_DIR) M=$$PWD

clean:
	rm *.o *.dtb*

dtoverlay-rpi: $(MODULE_NAME)-rpi.dtbo

dtoverlay-bbb: $(MODULE_NAME)-bbb.dtbo

dtoverlay: dtoverlay-$(BOARD)

dtoverlay-rpi-install: dtoverlay-rpi
	@echo "*** Installing the overlay to $(BOOT_PATH)/overlays, you will need to edit the $(DT_CONFIG) to enable the overlay."
	@echo "*** Typically, you would load the overlay by adding 'dtoverlay=gps_quadrino' to the config file."
	mkdir -p $(BOOT_PATH)/overlays
	cp $(MODULE_NAME)-rpi.dtbo $(BOOT_PATH)/overlays/

dtoverlay-bbb-install: dtoverlay-bbb
	@echo "*** Installing the overlay to /lib/firmware, you will need to edit the $(DT_CONFIG) to enable the overlay."
	@echo "*** Typically, you would load the overlay by adding 'cape_enable=bone_capemgr.enable_partno=gps_quadrino-bbb' to the config file."
	cp $(MODULE_NAME)-bbb.dtbo /lib/firmware/$(MODULE_NAME)-bbb-00A0.dtbo
	# regenerate the initram disk so that it will include the new dtbo firmware
	update-initramfs -uk `uname -r`

dtoverlay-install: dtoverlay-$(BOARD)-install

install: dtoverlay-install
	mkdir -p $(MODULE_INSTALL_PATH)
	cp $(MODULE_NAME).ko $(MODULE_INSTALL_PATH)
	depmod -a


install_link: dtoverlay_install
	mkdir -p $(MODULE_INSTALL_PATH)
	ln -s `pwd`/$(MODULE_NAME).ko $(MODULE_INSTALL_PATH)/$(MODULE_NAME).ko
	depmod -a

test:
	gcc -I/usr/include -I/usr/local/include test.c -o test && ./test

%.dtbo : %.dts
	dtc -@ -I dts -O dtb -o $@ $<


endif

