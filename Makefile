# makefile to build all linux related plugins in this plugin project.

all:
	$(MAKE) -C leechcore_ft601_driver_linux
	$(MAKE) -C leechcore_device_rawtcp
	$(MAKE) -C leechcore_device_microvmi
	$(MAKE) -C leechcore_device_qemu
	$(MAKE) -C leechcore_device_qemu_pcileech
	$(MAKE) -C leechcore_device_skeleton

clean:
	$(MAKE) -C leechcore_ft601_driver_linux clean
	$(MAKE) -C leechcore_device_rawtcp clean
	$(MAKE) -C leechcore_device_microvmi clean
	$(MAKE) -C leechcore_device_qemu clean
	$(MAKE) -C leechcore_device_qemu_pcileech clean
	$(MAKE) -C leechcore_device_skeleton clean
