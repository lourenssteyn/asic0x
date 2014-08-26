KERNEL_VERSION ?= $(shell uname -r)
MDIR := /lib/modules/$(KERNEL_VERSION)
KDIR := $(MDIR)/build
PWD := $(shell pwd)

obj-m += asic0x.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean: 
	make -C $(KDIR) M=$(PWD) clean
