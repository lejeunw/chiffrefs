#CFLAGS += -w 
TARGET = chiffrefs

chiffrefs-objs += file-mmu.o inode.o
obj-m += chiffrefs.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
