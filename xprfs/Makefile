TARGET := xprfs
obj-m = $(TARGET).o
$(TARGET)-objs = namei.o super.o file.o dir.o inode.o

VAR = $(shell mount | grep xprfs)
KERNDIR = /lib/modules/$(shell uname -r)/build
  
default:
	make -C $(KERNDIR) M=$(PWD) modules
	@echo "Compile success"
install:
	@make default
	insmod $(TARGET).ko
	@echo "Install success"
uninstall:
	@echo "Please umount first"
	rmmod $(TARGET)
	@echo "Uninstall success"
clean:
	make -C $(KERNDIR) M=$(PWD) modules clean  
	rm -rf modules.order
	@echo "Clean success"