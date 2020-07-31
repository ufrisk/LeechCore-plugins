LeechCore Plugins
===============================
This repository contains various plugins for [LeechCore - Physical Memory Acquisition Library](https://github.com/ufrisk/LeechCore).

Plugins are related to various kinds of device drivers allowing for modular extensive memory acquisition in various scenarios.



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



## leechcore_device_sp605tcp

#### Authors:
- Ulf Frisk
- Dmytro Oleksiuk

#### Supported Platforms:
- Windows, Linux

#### Overview:
Allows LeechCore to connect to a SP605 FPGA board exposing a TCP server on its network interface as described in the blog post by Dmytro. Requires a SP605 board flashed with the bitstream by [@d_olex](https://twitter.com/d_olex) as described in the following [README and Github project](https://github.com/Cr4sh/s6_pcie_microblaze).

#### Installation instructions:
Place leechcore_device_sp605tcp.[so|dll] alongside leechcore.[so|dll].


