#ifndef QEMU_DRIVER_H
#define QEMU_DRIVER_H

void *qemu_init(const char * mem_path);
bool qemu_read_physical(void *qemu_ram, uint64_t physical_address, uint8_t *buffer, size_t size, uint64_t * bytes_read);
bool qemu_write_physical(void *qemu_ram, uint64_t physical_address, uint8_t *buffer, size_t size);
bool qemu_get_max_physical_addr(void *qemu_ram, uint64_t *address_ptr);
void qemu_destroy(void * qemu_ram);
#endif 
