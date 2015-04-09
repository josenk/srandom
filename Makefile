TARGET_MODULE:=srandom
obj-m += $(TARGET_MODULE).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

load:
	insmod ./$(TARGET_MODULE).ko

unload:
	rmmod $(TARGET_MODULE).ko

install:
	mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)
	install -m 644  ./$(TARGET_MODULE).ko /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)
	install -m 644  ./$(TARGET_MODULE).conf /etc/modules-load.d/
	install -m 644  11-$(TARGET_MODULE).rules /etc/udev/rules.d/
	install -m 755  ./$(TARGET_MODULE) /usr/bin/$(TARGET_MODULE)
	depmod
	udevadm trigger
	@echo "Install Success."

uninstall:
	rm /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)/$(TARGET_MODULE).ko
	rm /etc/modules-load.d/$(TARGET_MODULE).conf
	rm /etc/udev/rules.d/*-$(TARGET_MODULE).rules
	rmdir --ignore-fail-on-non-empty /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)/
	depmod
	rm /usr/bin/$(TARGET_MODULE)
	@test -c /dev/srandom|| echo "Reboot required to complete uninstall."
	@echo "Uninstalled."