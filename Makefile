TARGET_MODULE:=srandom
obj-m += $(TARGET_MODULE).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	@$(MAKE) sign-module

sign-module:
	@if grep -q "CONFIG_MODULE_SIG=y" /boot/config-$(shell uname -r) 2>/dev/null; then \
		echo "Kernel requires signed modules. Signing $(TARGET_MODULE).ko..."; \
		SIGN_FILE_SCRIPT=""; \
		PRIV_KEY=""; \
		CERT_KEY=""; \
		if [ -f /lib/modules/$(shell uname -r)/build/scripts/sign-file ]; then \
			SIGN_FILE_SCRIPT="/lib/modules/$(shell uname -r)/build/scripts/sign-file"; \
		elif [ -f /usr/src/linux-headers-$(shell uname -r)/scripts/sign-file ]; then \
			SIGN_FILE_SCRIPT="/usr/src/linux-headers-$(shell uname -r)/scripts/sign-file"; \
		fi; \
		if [ -f /lib/modules/$(shell uname -r)/build/certs/signing_key.pem ]; then \
			PRIV_KEY="/lib/modules/$(shell uname -r)/build/certs/signing_key.pem"; \
			CERT_KEY="/lib/modules/$(shell uname -r)/build/certs/signing_key.x509"; \
		elif [ -f /usr/src/linux-headers-$(shell uname -r)/certs/signing_key.pem ]; then \
			PRIV_KEY="/usr/src/linux-headers-$(shell uname -r)/certs/signing_key.pem"; \
			CERT_KEY="/usr/src/linux-headers-$(shell uname -r)/certs/signing_key.x509"; \
		elif [ -f /usr/src/kernel-keys/signing_key.priv ]; then \
			PRIV_KEY="/usr/src/kernel-keys/signing_key.priv"; \
			CERT_KEY="/usr/src/kernel-keys/signing_key.x509"; \
		fi; \
		if [ -n "$$SIGN_FILE_SCRIPT" ] && [ -n "$$PRIV_KEY" ] && [ -f "$$PRIV_KEY" ] && [ -f "$$CERT_KEY" ]; then \
			if [ ! -r "$$PRIV_KEY" ]; then \
				echo "Error: Cannot read private key file $$PRIV_KEY"; \
				echo "Please run: sudo chmod 600 $$PRIV_KEY && sudo chown root:root $$PRIV_KEY"; \
				echo "And run make with: sudo make"; \
				exit 1; \
			fi; \
			$$SIGN_FILE_SCRIPT sha256 $$PRIV_KEY $$CERT_KEY ./$(TARGET_MODULE).ko; \
			echo "Module signed successfully."; \
		else \
			echo "Warning: Kernel signing keys not found. Module will not be signed."; \
			echo "To fix this, run the following commands:"; \
			echo "  sudo mkdir -p /usr/src/kernel-keys"; \
			echo "  sudo openssl genrsa -out /usr/src/kernel-keys/signing_key.priv 2048"; \
			echo "  sudo openssl req -new -x509 -key /usr/src/kernel-keys/signing_key.priv \\"; \
			echo "    -out /usr/src/kernel-keys/signing_key.x509 -days 3650 \\"; \
			echo "    -subj \"/C=US/ST=Local/L=Local/O=Local/CN=Module Signing Key\""; \
			echo "  sudo chmod 600 /usr/src/kernel-keys/signing_key.priv"; \
			echo "  sudo chmod 644 /usr/src/kernel-keys/signing_key.x509"; \
			echo "Then rebuild with 'sudo make clean && sudo make'"; \
		fi; \
	fi

generate-keys:
	@echo "Generating module signing keys..."
	sudo mkdir -p /usr/src/kernel-keys
	sudo openssl genrsa -out /usr/src/kernel-keys/signing_key.priv 2048
	sudo openssl req -new -x509 -key /usr/src/kernel-keys/signing_key.priv \
		-out /usr/src/kernel-keys/signing_key.x509 -days 3650 \
		-subj "/C=US/ST=Local/L=Local/O=Local/CN=Module Signing Key"
	@echo "Converting certificate to DER format for MOK..."
	sudo openssl x509 -in /usr/src/kernel-keys/signing_key.x509 -outform DER \
		-out /usr/src/kernel-keys/signing_key.der
	sudo chmod 600 /usr/src/kernel-keys/signing_key.priv
	sudo chmod 644 /usr/src/kernel-keys/signing_key.x509
	sudo chmod 644 /usr/src/kernel-keys/signing_key.der
	sudo chown root:root /usr/src/kernel-keys/signing_key.*
	@echo "Keys generated successfully in both PEM and DER formats."
	@echo "Now run 'sudo make' to build and sign the module."

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

load:
	@echo "Attempting to load $(TARGET_MODULE) module..."
	@if ! insmod ./$(TARGET_MODULE).ko 2>/dev/null; then \
		echo "Failed to load module. Checking for signature issues..."; \
		if dmesg | tail -5 | grep -q "Key was rejected\|signature"; then \
			echo "Module signature was rejected by kernel key service."; \
			echo ""; \
			echo "Solutions (choose one):"; \
			echo "1. Import signing key to kernel keyring:"; \
			echo "   sudo keyctl padd asymmetric \"\" %:.system_keyring < /usr/src/kernel-keys/signing_key.x509"; \
			echo ""; \
			echo "2. Disable signature enforcement temporarily:"; \
			echo "   sudo sysctl kernel.module_sig_enforce=0"; \
			echo "   then retry: make load"; \
			echo ""; \
			echo "3. Force load (bypass signature check):"; \
			echo "   make force-load"; \
			echo ""; \
			echo "4. Add to Machine Owner Key (MOK) for Secure Boot:"; \
			echo "   make mok-import"; \
			echo "   (requires reboot and MOK enrollment)"; \
		else \
			echo "Module load failed for other reasons. Check dmesg:"; \
			dmesg | tail -5; \
		fi; \
		exit 1; \
	else \
		echo "Module loaded successfully."; \
	fi

force-load:
	@echo "Force loading module (bypassing signature verification)..."
	@if [ "$$(id -u)" -ne 0 ]; then \
		echo "Force load requires root privileges. Run: sudo make force-load"; \
		exit 1; \
	fi
	@if grep -q "CONFIG_MODULE_SIG_FORCE=y" /boot/config-$$(uname -r) 2>/dev/null; then \
		echo "ERROR: Kernel has MODULE_SIG_FORCE=y. Cannot bypass signature check."; \
		echo "You must either:"; \
		echo "1. Import your signing key to the kernel keyring, or"; \
		echo "2. Boot with kernel parameter: module.sig_enforce=0"; \
		exit 1; \
	else \
		modprobe -f $(TARGET_MODULE) 2>/dev/null || \
		insmod -f ./$(TARGET_MODULE).ko 2>/dev/null || \
		(echo "Force load failed. Try: echo 0 > /proc/sys/kernel/modules_disabled" && \
		 sysctl kernel.module_sig_enforce=0 && \
		 insmod ./$(TARGET_MODULE).ko); \
	fi

import-key:
	@echo "Importing signing key to kernel keyring..."
	@if [ ! -f /usr/src/kernel-keys/signing_key.x509 ]; then \
		echo "Error: Signing key not found. Run 'make generate-keys' first."; \
		exit 1; \
	fi
	sudo keyctl padd asymmetric "" %:.system_keyring < /usr/src/kernel-keys/signing_key.x509
	@echo "Key imported successfully. Now try 'make load'"

mok-import:
	@echo "Importing signing key to Machine Owner Key (MOK) database..."
	@if [ ! -f /usr/src/kernel-keys/signing_key.der ]; then \
		echo "DER certificate not found. Generating it..."; \
		if [ ! -f /usr/src/kernel-keys/signing_key.x509 ]; then \
			echo "Error: No signing keys found. Run 'make generate-keys' first."; \
			exit 1; \
		fi; \
		sudo openssl x509 -in /usr/src/kernel-keys/signing_key.x509 -outform DER \
			-out /usr/src/kernel-keys/signing_key.der; \
		sudo chmod 644 /usr/src/kernel-keys/signing_key.der; \
		sudo chown root:root /usr/src/kernel-keys/signing_key.der; \
	fi
	sudo mokutil --import /usr/src/kernel-keys/signing_key.der
	@echo "MOK import scheduled. You must reboot and enroll the key when prompted."
	@echo "After reboot, the key will be trusted by Secure Boot."

unload:
	rmmod $(TARGET_MODULE).ko

install:
	mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)
	install -m 644  ./$(TARGET_MODULE).ko /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)
	install -m 644  ./11-$(TARGET_MODULE).rules /etc/udev/rules.d/
	install -m 755  ./$(TARGET_MODULE) /usr/bin/$(TARGET_MODULE)
	install -m 644  ./$(TARGET_MODULE).conf /etc/modules-load.d/
	depmod
	udevadm trigger
	@echo "Install Success."

uninstall:
	rm -f /lib/modules/$(shell uname -r)/kernel/drivers/$(TARGET_MODULE)/$(TARGET_MODULE).ko
	rm -f /etc/udev/rules.d/11-$(TARGET_MODULE).rules
	rm -f /etc/modules-load.d/$(TARGET_MODULE).conf
	depmod
	rm -f /usr/bin/$(TARGET_MODULE)
	@test -c /dev/srandom|| echo "Reboot required to complete uninstall."
	@echo "Uninstalled."
