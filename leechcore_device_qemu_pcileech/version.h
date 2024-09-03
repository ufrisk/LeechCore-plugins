#ifndef STRINGIZE
#define STRINGIZE2(s) #s
#define STRINGIZE(s) STRINGIZE2(s)
#endif

#define VERSION_MAJOR               2
#define VERSION_MINOR               18
#define VERSION_REVISION            8
#define VERSION_BUILD               76

#define VER_FILE_DESCRIPTION_STR    "LeechCorePlugin : QEMU PCILeech"
#define VER_FILE_VERSION            VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION, VERSION_BUILD
#define VER_FILE_VERSION_STR        STRINGIZE(VERSION_MAJOR)        \
                                    "." STRINGIZE(VERSION_MINOR)    \
                                    "." STRINGIZE(VERSION_REVISION) \
                                    "." STRINGIZE(VERSION_BUILD)    \

#define VER_COMPANY_NAME_STR        ""
#define VER_PRODUCTNAME_STR         "LeechCorePluginQEMUPCILeech"
#define VER_PRODUCT_VERSION         VER_FILE_VERSION
#define VER_PRODUCT_VERSION_STR     VER_FILE_VERSION_STR
#define VER_ORIGINAL_FILENAME_STR   "leechcore_device_qemupcileech.dll"
#define VER_INTERNAL_NAME_STR       VER_ORIGINAL_FILENAME_STR
#define VER_COPYRIGHT_STR           "Copyright (c) Zero Tang - tangptr.com"