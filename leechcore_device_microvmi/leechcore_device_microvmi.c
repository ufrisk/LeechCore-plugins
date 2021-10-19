#include <stdbool.h>

#include <leechcore_device.h>
#include <libmicrovmi.h>

#define PLUGIN_URL_SCHEME "microvmi://"

static VOID DeviceMicrovmi_ReadContigious(PLC_READ_CONTIGIOUS_CONTEXT ctxRC) {
    // read contiguous physical memory
    PLC_CONTEXT ctxLC = ctxRC->ctxLC;
    void *driver = ctxLC->hDevice;
    uint64_t bytes_read = 0;
    if (!microvmi_read_physical(driver, ctxRC->paBase, ctxRC->pb, ctxRC->cb,
                                &bytes_read)) {
        lcprintfvvv(ctxLC, "Failed to read physical memory at 0x%llx\n",
                    ctxRC->paBase);
    }
    ctxRC->cbRead = (DWORD)bytes_read;
}

static BOOL DeviceMicrovmi_WriteContigious(_In_ PLC_CONTEXT ctxLC,
                                           _In_ QWORD qwAddr, _In_ DWORD cb,
                                           _In_reads_(cb) PBYTE pb) {
    // write contiguous memory
    void *driver = ctxLC->hDevice;
    if (!microvmi_write_physical(driver, qwAddr, pb, cb)) {
        lcprintfvvv(ctxLC,
                    "Failed to write %d bytes in physical memory at 0x%llx\n",
                    cb, qwAddr);
        return false;
    }
    return true;
}

static VOID DeviceMicrovmi_Close(_Inout_ PLC_CONTEXT ctxLC) {
    // close driver
    void *driver = ctxLC->hDevice;
    microvmi_destroy(driver);
}

static bool parse_url_args(PLC_CONTEXT ctxLC,
                           DriverInitParamsFFI *init_params) {
    // this function parses the device URL string and fills the init_params struct in consequence
    // the URL syntax is the following
    //      microvmi://param1=value1&param2=value2

    const char *url_device = ctxLC->Config.szDevice;
    // check URL scheme
    if (strncmp(url_device, PLUGIN_URL_SCHEME, strlen(PLUGIN_URL_SCHEME))) {
        // no match, quit
        return false;
    }
    // clone URL as strtok will modify it in place
    char *szDevice_start_params =
        strdup(url_device + strlen(PLUGIN_URL_SCHEME));
    // split on '&'
    char *saveptr = NULL;
    char *token = NULL;
    for (token = strtok_r(szDevice_start_params, "&", &saveptr); token != NULL;
         token = strtok_r(NULL, "&", &saveptr)) {
        // token is param1=value1
        // split on '='
        char *saveptr2 = NULL;
        char *param_name = strtok_r(token, "=", &saveptr2);
        if (!param_name)
            continue;
        char *param_value = strtok_r(NULL, "=", &saveptr2);
        if (!param_value)
            continue;
        if (!strncmp(param_name, "vm_name", strlen("vm_name"))) {
            // free previous value if any
            if (init_params->common.vm_name) {
                free(init_params->common.vm_name);
                init_params->common.vm_name = NULL;
            }
            init_params->common.vm_name = strdup(param_value);
        } else if (!strncmp(param_name, "kvm_unix_socket",
                            strlen("kvm_unix_socket"))) {
            init_params->kvm.tag = UnixSocket;
            // free previous value if any
            if (init_params->kvm.unix_socket.path) {
                free(init_params->kvm.unix_socket.path);
                init_params->kvm.unix_socket.path = NULL;
            }
            init_params->kvm.unix_socket.path = strdup(param_value);
        } else if (!strncmp(param_name, "memflow_connector_name",
                            strlen("memflow_connector_name"))) {
            // free previous value if any
            if (init_params->memflow.connector_name) {
                free(init_params->memflow.connector_name);
                init_params->memflow.connector_name = NULL;
            }
            init_params->memflow.connector_name = strdup(param_value);
        } else {
            lcprintfv(ctxLC, "MICROVMI: unhandled init parameter: %s\n",
                      param_name);
        }
    }
    free(szDevice_start_params);
    return true;
}

_Success_(return ) EXPORTED_FUNCTION BOOL
    LcPluginCreate(_Inout_ PLC_CONTEXT ctxLC,
                   _Out_opt_ PPLC_CONFIG_ERRORINFO ppLcCreateErrorInfo) {
    lcprintfv(ctxLC, "MICROVMI: Initializing\n");
    // safety checks
    if (ppLcCreateErrorInfo) {
        *ppLcCreateErrorInfo = NULL;
    }
    if (ctxLC->version != LC_CONTEXT_VERSION) {
        return false;
    }

    // handle init args
    DriverInitParamsFFI init_params = {0};
    if (!parse_url_args(ctxLC, &init_params)) {
        return false;
    }

    // init libmicrovmi
    microvmi_envlogger_init();
    const char *init_error = NULL;

    void *microvmi_driver = microvmi_init(NULL, &init_params, &init_error);
    if (!microvmi_driver) {
        lcprintfv(ctxLC, "MICROVMI: initialization failed: %s.\n", init_error);
        goto error_exit;
    }

    // assign context
    ctxLC->hDevice = (HANDLE)microvmi_driver;

    // setup config
    ctxLC->Config.fVolatile = true;
    // set max physical address
    uint64_t max_addr = 0;
    if (!microvmi_get_max_physical_addr(microvmi_driver, &max_addr)) {
        lcprintf(ctxLC, "Failed to get max physical address\n");
        goto error_exit;
    }
    lcprintfvv(ctxLC, "MICROVMI: max physical address: 0x%lx\n", max_addr);
    ctxLC->Config.paMax = max_addr;
    // set callback functions
    ctxLC->pfnReadContigious = DeviceMicrovmi_ReadContigious;
    ctxLC->pfnWriteContigious = DeviceMicrovmi_WriteContigious;
    ctxLC->pfnClose = DeviceMicrovmi_Close;
    // status
    lcprintfv(ctxLC, "MICROVMI: initialized.\n");
    return true;
error_exit:
    if (init_error)
        rs_cstring_free((char *)init_error);
    // free init_params
    if (init_params.common.vm_name)
        free(init_params.common.vm_name);
    if (init_params.kvm.unix_socket.path)
        free(init_params.kvm.unix_socket.path);
    if (init_params.memflow.connector_name)
        free(init_params.memflow.connector_name);
    DeviceMicrovmi_Close(ctxLC);
    return false;
}
