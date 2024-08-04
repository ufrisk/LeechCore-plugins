LeechCore Plugins
===============================
This repository contains various plugins for [LeechCore - Physical Memory Acquisition Library](https://github.com/ufrisk/LeechCore).

Plugins are related to various kinds of device drivers allowing for modular extensive memory acquisition in various scenarios.

## Table of Contents

- [leechdma_driver_linux](#leechdma_driver_linux)
- [leechcore_ft601_driver_linux](#leechcore_ft601_driver_linux)
- [leechcore_device_hvsavedstate](#leechcore_device_hvsavedstate)
- [leechcore_device_rawtcp](#leechcore_device_rawtcp)
- [leechcore_device_microvmi](#leechcore_device_microvmi)
- [leechcore_device_qemu](#leechcore_device_qemu)
- [leechcore_device_skeleton](#leechcore_device_skeleton)



## leechcore_device_hvsavedstate

#### Authors:
- Ulf Frisk
- Matt Suiche - www.comae.com

#### Supported Platforms:
- Windows

#### Overview:
The leechcore_device_hvsavedstate library allows applications access to a access the memory of Hyper-V saved state files (.vmrs). The library depends on the `vmsavedstatedumpprovider.dll` library from Microsoft. It must be placed in the same folder as the LeechCore. The library exists in the most recent Windows SDK and is usually found in the location: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.17763.0\x64\vmsavedstatedumpprovider.dll`.

#### Installation instructions:
Place leechcore_device_hvsavedstate.dll and vmsavedstatedumpprovider.dll alongside leechcore.dll.



## leechcore_device_microvmi

#### Authors
- Mathieu Tarral ([@mtarral](https://github.com/mtarral)) - [ANSSI](https://www.ssi.gouv.fr/)

#### Supported Platforms
- Linux

#### Overview

Allows LeechCore to peek into the live physical memory of virtual machines
supported by [libmicrovmi](https://wenzel.github.io/libmicrovmi/reference/drivers.html)

#### Requirements

- [libmicrovmi](https://github.com/Wenzel/libmicrovmi): see the [documentation](https://wenzel.github.io/libmicrovmi/tutorial/installation.html)

#### Plugin documentation

- URL device syntax: `microvmi://param1=value1&param2=value2`
- Debugging: `export RUST_LOG=debug`

##### Xen

Parameters:
- `vm_name`: name of the VM

~~~
sudo -E ./memprocfs -mount xxx -device 'microvmi://vm_name=win10'
~~~

##### KVM

Parameters:
- `vm_name`: name of the VM
- `kvm_unix_socket`: KVMi UNIX socket  (see KVM-VMI project)

~~~
./memprocfs -mount xxx -device 'microvmi://vm_name=win10&kvm_unix_socket=/tmp/introspector'
~~~

##### VirtualBox ([IceBox](https://github.com/thalium/icebox))

Parameters:
- `vm_name`: name of the VM

~~~
./memprocfs -mount xxx -device 'microvmi://vm_name=win10'
~~~

##### QEMU

Parameters:
- `memflow_connector_name`: `qemu_procfs`
- `vm_name` (optional): name of the VM

~~~
sudo -E ./memprocfs -mount xxx -device 'microvmi://memflow_connector_name=qemu_procfs'
~~~



## leechcore_device_qemu

#### Authors:
- Mathieu Renard - www.h2lab.org
- [Aodzip](https://github.com/aodzip)
- Ulf Frisk

#### Supported Platforms:
- Linux

#### Overview:

Parameters:
- `shm`: `filename` of shared memory file in /dev/shm/xxxx (if shared memory acquisition method is used).
- `hugepage-pid=`: libvirt / QEMU process to target (if hugepage acquisition method is used).
- `qmp`: `path` to optional qmp socket (used to query vm memory ranges, optional).
- `delay-latency-ns`: Delay in ns to be applied once each read request (optional).
- `delay-readpage-ns`: Delay in ns to be applied per read page (optional).

##### QEMU Virtual machine setup

**Also see the more extensive [QEMU documentation in the LeechCore Wiki](https://github.com/ufrisk/LeechCore/wiki/Device_QEMU).**

To enable the memory backend on our virtual machine, we need to add the memory-backend-object to our command line.

1. Add the memory-backend-object to the command line 

```
 memory-backend-file,id=mem,size=512M,mem-path=/dev/shm/qemu-ram,share=on
```

2. Launch the virtual machine

~~~
 qemu-system-x86_64 -kernel vmlinuz.x86_64 -m 512  -drive format=raw,file=debian.img,if=virtio,aio=native,cache.direct=on, \
                    -enable-kvm -append "root=/dev/mapper/cl-root console=ttyS0 earlyprintk=serial,ttyS0,115200 nokaslr"   \ 
                    -initrd initramfs.x86_64.img \
                    -object memory-backend-file,id=mem,size=512M,mem-path=/dev/shm/qemu-ram,share=on \
                    -qmp unix:/tmp/qmp.sock,server,nowait

~~~

##### PCILeech
~~~
./pcileech -device 'qemu://shm=qemu-ram' write -min 0x12345678 -in 0xdeadcafe
~~~

##### Memprocfs
~~~
sudo -E ./memprocfs -mount xxx -device 'qemu://shm=qemu-ram,qmp=/tmp/qmp.sock'
~~~

## leechcore_device_qemupcileech

#### Authors
- Zero Tang - tangptr.com

#### Supported Platforms
- Windows, Linux

#### Overview
Allows LeechCore to connect to a "raw tcp" server hosted by QEMU to perform DMA attacks against the guest inside QEMU. The main purpose of this plugin is to allow security researchers to easily perform DMA attacks and test their IOMMU defenses.

#### Installation Instructions
Place leechcore_device_qemupcileech.[so|dll] alongside leechcore.[so|dll].

#### QEMU Guide
A [patch](https://lists.nongnu.org/archive/html/qemu-devel/2024-08/msg00526.html) is submitted to QEMU but it hasn't been merged into the official repository yet. You can build QEMU on your own and apply this patch to test it for yourself. Use [git apply](https://git-scm.com/docs/git-apply) command to add the patch into the code.

Launch the VM with virtual PCILeech device:
```
qemu-system-x86_64 -device pcileech,host=0.0.0.0,port=6789
```
You can omit `host` and `port` arguments. `host` is default to `0.0.0.0` and `port` is default to `6789`. \
Append more arguments to fit your VM settings.

Invoke PCILeech:
```
pcileech -device qemupcileech://127.0.0.1:6789 display -min 0x3800000
```
Replace the IP address and port.

## leechcore_device_rawtcp

#### Authors:
- Ulf Frisk
- Synacktiv - www.synacktiv.com

#### Supported Platforms:
- Windows, Linux

#### Overview:
Allows LeechCore to connect to a "raw tcp" server which may be used to perform DMA attacks against a compromised iLO interface as described in the [blog entry by Synacktiv](https://www.synacktiv.com/posts/exploit/using-your-bmc-as-a-dma-device-plugging-pcileech-to-hpe-ilo-4.html) amongst other things.

#### Installation instructions:
Place leechcore_device_rawtcp.[so|dll] alongside leechcore.[so|dll].



## leechcore_device_skeleton

#### Author:
- Rick Wertenbroek

#### Supported Platfoms:
- All

#### Overview:
The leechcore_device_skeleton library is a simple skeleton plugin that only displays the memory read and write requests without doing anything. It is meant to be used as a skeleton to write further plugins.

#### Plugin documentation:
This plugin only have two parameters `dev` and the optional `size` parameter. The `dev` parameter is required but does nothing, it is meant to demonstrate parameter parsing and checking. The optional `size` parameter defines the memory region size, by default 4 GB, this parameter is meant to limit the memory range for testing and to demonstrate `size_t` type paramter parsing.

Example commands :
- `./pcileech dump -min 0x0 -max 0x10000 -device 'skeleton://dev=/dev/dummy'`
- `./pcileech dump -min 0x0 -max 0x10000 -device 'skeleton://dev=/dev/dummy,size=35536'`



## leechcore_ft601_driver_linux

#### Authors:
- Ulf Frisk
- Jérémie Boutoille from Synacktiv - www.synacktiv.com
- Based on PCIeScreamer kernel driver from LambdaConcept.

#### Supported Platforms:
- Linux

#### Overview:
The leechcore_ft601_driver_linux library allows applications access to a limited version of API calls the FT601 FTD3XX.dll Windows library from ftdichip provided. This allows applications to use a limited FTD3XX.dll compatible application library on Linux. This library does not require LeechCore to function and may be used in other applications as well.

The library requires libusb (`apt-get install libusb-1.0-0`) and access to the usb device (permission change or run as root may be required) alternatively a [Kernel Driver](https://github.com/lambdaconcept/ft60x_driver) provided by LambdaConcept. LeechCore will automatically attempt to locate the kernel driver before using libusb as fallback.

#### Installation instructions:
Place leechcore_ft601_driver_linux.so alongside leechcore.so.



## leechdma_driver_linux

#### Authors:
- Ulf Frisk

#### Supported Platforms:
- Linux

#### License:
- GPLv2

#### Overview:

The LeechDMA linux kernel driver allows the user to compile a kernel module (.ko) which may be inserted into the kernel. When a LeechDMA device (ZDMA or similar) is connected a device will show up as `/dev/leechdma*` where `*` is a number. User mode programs can then communicate with the LeechDMA linux kernel driver and its connected device.
