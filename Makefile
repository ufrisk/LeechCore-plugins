# makefile to build all linux related plugins in this plugin project.

all:
	$(MAKE) -C leechcore_ft601_driver_linux
	$(MAKE) -C leechcore_device_rawtcp
	$(MAKE) -C leechcore_device_sp605tcp
	$(MAKE) -C leechcore_device_microvmi

clean:
	$(MAKE) -C leechcore_ft601_driver_linux clean
	$(MAKE) -C leechcore_device_rawtcp clean
	$(MAKE) -C leechcore_device_sp605tcp clean
	$(MAKE) -C leechcore_device_microvmi clean
