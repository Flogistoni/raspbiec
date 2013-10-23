# This Makefile assumes that you have a Raspberry Pi cross-compiling environment
# and the variables KERNEL_SRC and CCPREFIX are set.
#
# Example:
# export KERNEL_SRC=/home/nn/pikernel/linux
# export CCPREFIX=/home/nn/pikernel/tools/arm-bcm2708/arm-bcm2708hardfp-linux-gnueabi/bin/arm-bcm2708hardfp-linux-gnueabi-

TARGET = raspbiecdrv

all: checkvars raspbiec raspbiecdrv

checkvars:
ifeq ($(strip $(CCPREFIX)),)
	$(error CCPREFIX not set (path prefix to cross-compiler binaries))
endif
ifeq ($(strip $(KERNEL_SRC)),)
	$(error KERNEL_SRC not set (path to kernel source))
endif

raspbiec: raspbiec.o raspbiec_device.o raspbiec_utils.o
	${CCPREFIX}g++ $^ -o $@

raspbiec.o: raspbiec.cpp raspbiec.h raspbiec_device.h raspbiec_utils.h raspbiec_common.h
	${CCPREFIX}g++ -c $<

raspbiec_device.o: raspbiec_device.cpp raspbiec_device.h raspbiec_utils.h raspbiec_common.h
	${CCPREFIX}g++ -c $<

raspbiec_utils.o: raspbiec_utils.cpp raspbiec_utils.h raspbiec_common.h
	${CCPREFIX}g++ -c $<

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m	:= $(TARGET).o

else



KERNELDIR ?= ${KERNEL_SRC}
PWD       := $(shell pwd)

raspbiecdrv:
	$(MAKE)  ARCH=arm CROSS_COMPILE=${CCPREFIX} -C $(KERNELDIR) SUBDIRS=$(PWD) modules

endif

clean:
	rm -rf *.o *.ko *~ core .depend *.mod.c .*.cmd .tmp_versions .*.o.d

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend

ins: default rem
	insmod $(TARGET).ko debug=1

rem:
	@if [ -n "`lsmod | grep -s $(TARGET)`" ]; then rmmod $(TARGET); echo "rmmod $(TARGET)"; fi

ifeq (.depend,$(wildcard .depend))
include .depend
endif
