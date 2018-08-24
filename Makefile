KVER  := $(shell uname -r)
KSRC := /lib/modules/$(KVER)/build

export CONFIG_SYSCALL_OVERRIDE = m

EXTRA_CFLAGS += $(USER_EXTRA_CFLAGS)
EXTRA_CFLAGS += -I$(src)/include

obj-m += syscall_override.o

all: modules

modules:
	$(MAKE) -C $(KSRC) M=$(shell pwd)  modules
