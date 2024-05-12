Installation Instructions:
==========================

Make sure you have the latest release of PCILeech and/or MemProcFS.
https://github.com/ufrisk/PCILeech/releases/latest
https://github.com/ufrisk/MemProcFS/releases/latest

Make sure gcc and kernel headers are installed on the Linux system.

Run the following commands to compile and install:
==================================================
make
insmod leechdma.ko
chmod a+rw /dev/leechdma*


It should now be possible to connect with PCILeech to your LeechDMA powered Thunderbolt device!
