obj-m += ar0234.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR)  M=$(shell pwd)

clean:
	make -C $(KDIR)  M=$(shell pwd) clean

%.dtbo: %.dts
	dtc -@ -I dts -O dtb -o $@ $<
