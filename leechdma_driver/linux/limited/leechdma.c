// leechdma.c : implementation of the leechdma device driver for thunderbolt/pcie transport.
//
// License GPLv2
//
// Installation instructions, run the following commands:
// make
// insmod leechdma.ko
// chmod a+rw /dev/leechdma*
//
// (c) Ulf Frisk, 2024
// Author: Ulf Frisk, pcileech@frizk.net
// https://github.com/ufrisk
//

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>

#define LEECHDMA_DRIVER_NAME    "leechdma"
#define LEECHDMA_VENDOR_ID      0x10ee
#define LEECHDMA_DEVICE_ID      0x0667
#define LEECHDMA_DEVICES_MAX    16
#define LEECHDMA_DMA_SIZE       1*1024*1024

#define LEECHDMA_IOCTL_VERSION_QUERY    _IOR('q', 32, uint32_t)
#define LEECHDMA_IOCTL_MMAP_NEXTRANGE   _IOW('e', 16, uint32_t*)

static const struct pci_device_id leechdma_pci_tbl[] = {
    { PCI_DEVICE(LEECHDMA_VENDOR_ID, LEECHDMA_DEVICE_ID), },
    { 0, }
};

typedef struct td_leechdma_ctx {
    int f_active;
    int id;
    struct pci_dev *pdev;
    uint32_t pa_bar0;
    uint32_t pa_bar1;
    uint8_t *bar0;
    uint8_t *bar1;
    atomic_t c_user_thread;
    struct {
        uint8_t *buf;
        dma_addr_t dma_handle;
    } dma[4];
    uint8_t *dma_status_buf;
    dma_addr_t dma_status_handle;
    struct {
        atomic_t f_open;            // is file handle opened?
        int f_active;
        struct cdev cdev;
    } chardev;
    uint32_t mmap_nextrange;
} leechdma_ctx_t;

// global context table (max 16 concurrent devices):
int leechdma_devices_active_count;
leechdma_ctx_t leechdma_devices[LEECHDMA_DEVICES_MAX];
static int leechdma_chrdev_major;
static struct class *leechdma_chrdev_class;
static dev_t leechdma_chrdev_dev_id;

//----------------------------------------------------------------------------
// Chardev functionality below:
// (chardev device linked as a sub-device to PCIe device for ease of access).
//----------------------------------------------------------------------------

/*
* CHARDEV OPEN implementation:
* -- inode
* -- file
* -- return
*/
static int leechdma_chardev_open(struct inode *inode, struct file *file)
{
    leechdma_ctx_t *ctx;
    int f_open;
    int minor = iminor(file->f_inode);
    if (minor >= LEECHDMA_DEVICES_MAX) {
        return -ENXIO;
    }
    ctx = &leechdma_devices[minor];
    if(!ctx->f_active) {
        return -ENXIO;
    }
    f_open = atomic_cmpxchg(&ctx->chardev.f_open, 0, 1);
    if(f_open == 1) {
        return -EBUSY;
    }
    return 0;
}

/*
* CHARDEV RELEASE implementation:
* -- inode
* -- file
* -- return
*/
static int leechdma_chardev_release(struct inode *inode, struct file *file)
{
    leechdma_ctx_t *ctx;
    int f_open;
    int minor = iminor(file->f_inode);
    if (minor >= LEECHDMA_DEVICES_MAX) {
        return -ENXIO;
    }
    ctx = &leechdma_devices[minor];
    if(!ctx->f_active) {
        return -ENXIO;
    }
    f_open = atomic_cmpxchg(&ctx->chardev.f_open, 1, 0);
    if(f_open == 0) {
        return -EBUSY;
    }
    return 0;
}

/*
* CHARDEV IOCTL implementation:
* -- file
* -- cmd
* -- arg
* -- return
*/
static long leechdma_chardev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    leechdma_ctx_t *ctx;
    int minor;
    uint32_t count = 0, version;
    long result = -EINVAL;
    minor = iminor(file->f_inode);
    ctx = leechdma_devices + minor;
    if ((minor >= LEECHDMA_DEVICES_MAX) || !ctx->f_active || !atomic_read(&ctx->chardev.f_open)) {
        return -ENXIO;
    }
    atomic_inc(&ctx->c_user_thread);
    // ioctl main processing:
    switch(cmd) {
        case LEECHDMA_IOCTL_MMAP_NEXTRANGE:
            if(copy_from_user(&count, (uint32_t*)arg, sizeof(count))) { goto fail; }
            if(count >= 0 && count <= 5) {
                ctx->mmap_nextrange = count;
                result = 0;
                goto finish;
            }
            ctx->mmap_nextrange = 0;
            goto finish;
        case LEECHDMA_IOCTL_VERSION_QUERY:
            version = ioread32(ctx->bar0 + 0x20);
            if(copy_to_user((uint32_t __user *)arg, &version, sizeof(version))) { goto fail; }
            result = 0;
            goto finish;
        default:
            goto finish;
    }
finish:
    atomic_dec(&ctx->c_user_thread);
    return result;
fail:
    atomic_dec(&ctx->c_user_thread);
    return -EFAULT;
}

/*
* CHARDEV MMAP implementation:
* -- file
* -- vma
* -- return
*/
static int leechdma_chardev_mmap(struct file *file, struct vm_area_struct *vma)
{
    leechdma_ctx_t *ctx;
    int minor;
    long result = -EINVAL;
    unsigned long vsize;
    minor = iminor(file->f_inode);
    ctx = leechdma_devices + minor;
    if ((minor >= LEECHDMA_DEVICES_MAX) || !ctx->f_active || !atomic_read(&ctx->chardev.f_open)) {
        return -ENXIO;
    }
    atomic_inc(&ctx->c_user_thread);
    // sanity check and mmap:
    vsize = vma->vm_end - vma->vm_start;
    switch(ctx->mmap_nextrange) {
        case 0:
        case 1:
        case 2:
        case 3:
            if(vsize == LEECHDMA_DMA_SIZE) {
                result = remap_pfn_range(vma, vma->vm_start, ctx->dma[ctx->mmap_nextrange].dma_handle >> PAGE_SHIFT, LEECHDMA_DMA_SIZE, vma->vm_page_prot);
            }
            break;
        case 4:
            if(vsize == 0x1000) {
                result = remap_pfn_range(vma, vma->vm_start, ctx->dma_status_handle >> PAGE_SHIFT, 0x1000, vma->vm_page_prot);
            }
            break;
        case 5:
            if(vsize == 0x1000) {
                result = remap_pfn_range(vma, vma->vm_start, ctx->pa_bar1 >> PAGE_SHIFT, 0x1000, vma->vm_page_prot);
            }
            break;
        default:
            break;
    }
    atomic_dec(&ctx->c_user_thread);
    return result;
}

static struct file_operations leechdma_chardev_fops = {
    .owner = THIS_MODULE,
    .open = leechdma_chardev_open,
    .release = leechdma_chardev_release,
    .unlocked_ioctl = leechdma_chardev_ioctl,
    .mmap = leechdma_chardev_mmap,
};

/*
* Initialize the static parts of the leechdma chardev device.
* Each individual chardev device will be initialized by function:
* leechdma_chardev_init_single().
*/
static int leechdma_chardev_init(void)
{
    int err;
    // 1: allocate chardev regions:
    err = alloc_chrdev_region(&leechdma_chrdev_dev_id, 0, LEECHDMA_DEVICES_MAX, LEECHDMA_DRIVER_NAME"-chrdev-region");
    if (err < 0) {
        printk(KERN_ERR "leechdma: failed alloc chardev regions.\n");
        return err;
    }
    leechdma_chrdev_major = MAJOR(leechdma_chrdev_dev_id);
    // Create class
    leechdma_chrdev_class = class_create(LEECHDMA_DRIVER_NAME"-chrdev-class");
    if (IS_ERR(leechdma_chrdev_class)) {
        printk(KERN_ERR "leechdma: failed alloc chardev class.\n");
        unregister_chrdev_region(leechdma_chrdev_dev_id, LEECHDMA_DEVICES_MAX);
        return PTR_ERR(leechdma_chrdev_class);
    }
    return 0;
}

/*
* Destroy the static parts of the leechdma chardev device.
*/
static void leechdma_chardev_exit(void)
{
    class_destroy(leechdma_chrdev_class);
    unregister_chrdev_region(leechdma_chrdev_dev_id, LEECHDMA_DEVICES_MAX);
}

/*
* Create the dynamic chardev device.
*/
static void leechdma_chardev_init_single(leechdma_ctx_t *ctx)
{
    if(!ctx->chardev.f_active) {
        ctx->chardev.f_active = 1;
        cdev_init(&ctx->chardev.cdev, &leechdma_chardev_fops);
        ctx->chardev.cdev.owner = THIS_MODULE;
        cdev_add(&ctx->chardev.cdev, MKDEV(leechdma_chrdev_major, ctx->id), 1);
        device_create(leechdma_chrdev_class, NULL, MKDEV(leechdma_chrdev_major, ctx->id), NULL, LEECHDMA_DRIVER_NAME"%d", ctx->id);
    }
}

/*
* Destroy the dynamic chardev device.
*/
static void leechdma_chardev_exit_single(leechdma_ctx_t *ctx)
{
    if(ctx->chardev.f_active) {
        ctx->chardev.f_active = 0;
        device_destroy(leechdma_chrdev_class, MKDEV(leechdma_chrdev_major, ctx->id));
        cdev_del(&ctx->chardev.cdev);
    }
}



//----------------------------------------------------------------------------
// PCIe driver functionality below:
//----------------------------------------------------------------------------

static void leechdma_ctx_destroy(leechdma_ctx_t *ctx)
{
    int i;
    leechdma_chardev_exit_single(ctx);
    if(ctx->bar0) { iounmap(ctx->bar0); }
    if(ctx->bar1) { iounmap(ctx->bar1); }
    for(i = 0; i < 4; i++) {
        if(ctx->dma[i].buf) { dma_free_coherent(&ctx->pdev->dev, LEECHDMA_DMA_SIZE, ctx->dma[i].buf, ctx->dma[i].dma_handle); }
    }
    if(ctx->dma_status_buf) { dma_free_coherent(&ctx->pdev->dev, 0x1000, ctx->dma_status_buf, ctx->dma_status_handle); }
    memset(ctx, 0, sizeof(leechdma_ctx_t));
    leechdma_devices_active_count--;
}

MODULE_DEVICE_TABLE(pci, leechdma_pci_tbl);

/*
* Interrupt handler for the leechdma device.
* -- irq
* -- dev_id
* -- return
*/
static irqreturn_t leechdma_interrupt_handler(int irq, void *dev_id)
{
    return IRQ_HANDLED;
}

static int leechdma_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int ret, i;
    leechdma_ctx_t *ctx = NULL;
    uint32_t pa_bar0, pa_bar1, cb_bar0, cb_bar1, off;
    // 1: fetch and initialize leechdma context:
    for (i = 0; i < LEECHDMA_DEVICES_MAX; i++) {
        if(!leechdma_devices[i].f_active) {
            leechdma_devices_active_count++;
            ctx = &leechdma_devices[i];
            ctx->f_active = 1;
            ctx->id = i;
            ctx->pdev = pdev;
            break;
        }
    }
    if (!ctx) {
        printk(KERN_ERR "leechdma: unable to reserve device slot.\n");
        return -EIO;
    }
    // 2: enable device:
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "leechdma: failed to enable device.\n");
        leechdma_ctx_destroy(ctx);
        return ret;
    }
    // 3: map io bars & verify:
    pa_bar0 = pci_resource_start(pdev, 0);
    pa_bar1 = pci_resource_start(pdev, 1);
    cb_bar0 = pci_resource_len(pdev, 0);
    cb_bar1 = pci_resource_len(pdev, 1);
    ret = pci_enable_device_mem(pdev);
    if (ret || !pa_bar0 || !pa_bar1 || (pa_bar0 & 0xfff) || (pa_bar1 & 0xfff) || (cb_bar0 != 0x1000) || (cb_bar1 != 0x1000) || !(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) || !(pci_resource_flags(pdev, 1) & IORESOURCE_MEM)) {
        printk(KERN_ERR "leechdma: invalid io bar memory regions.\n");
        pci_disable_device(pdev);
        leechdma_ctx_destroy(ctx);
        return -EIO;
    }
    ctx->bar0 = ioremap(pa_bar0, cb_bar0); ctx->pa_bar0 = pa_bar0;
    ctx->bar1 = ioremap(pa_bar1, cb_bar1); ctx->pa_bar1 = pa_bar1;
    if (!ctx->bar0 || !ctx->bar1) {
        printk(KERN_ERR "leechdma: failed to map io bar memory regions. %p %p\n", ctx->bar0, ctx->bar1);
        pci_disable_device(pdev);
        leechdma_ctx_destroy(ctx);
        return -EIO;
    }
    for(i = 0; i < 16; i += 4) {
        if(ioread32(ctx->bar0 + i) != *(unsigned int*)("LEECHDMA DEVICE " + i)) {
            printk(KERN_ERR "leechdma: failed - not a leechdma device.\n");
            pci_disable_device(pdev);
            leechdma_ctx_destroy(ctx);
            return -EIO;
        }
    }
    // 4: allocate dma memory regions
    dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
    for(i = 0; i < 4; i++) {
        ctx->dma[i].buf = dma_alloc_coherent(&pdev->dev, LEECHDMA_DMA_SIZE, &ctx->dma[i].dma_handle, GFP_KERNEL);
    }
    ctx->dma_status_buf = dma_alloc_coherent(&pdev->dev, 0x1000, &ctx->dma_status_handle, GFP_KERNEL);
    if(!ctx->dma[0].buf || !ctx->dma[1].buf || !ctx->dma[2].buf || !ctx->dma[3].buf || !ctx->dma_status_buf) {
        printk(KERN_ERR "leechdma: failed to allocate dma memory buffers.\n");
        pci_disable_device(pdev);
        leechdma_ctx_destroy(ctx);
        return -EIO;
    }
    // 5: request irq:
    ret = request_irq(pdev->irq, leechdma_interrupt_handler, IRQF_SHARED, LEECHDMA_DRIVER_NAME, pdev);
    if (ret) {
        printk(KERN_ERR "leechdma: failed to request IRQ.\n");
        pci_disable_device(pdev);
        leechdma_ctx_destroy(ctx);
        return ret;
    }
    // 5: create chardev device:
    leechdma_chardev_init_single(ctx);
    // 6: provision the device:
    memset_io(ctx->bar1, 0, 0x100);
    for(i = 0; i < 4; i++) {
        off = i * 0x10;
        iowrite32((u32)ctx->dma_status_handle + off, ctx->bar0 + 0x100 + off + 0x08);
        iowrite32(LEECHDMA_DMA_SIZE, ctx->bar0 + 0x100 + off + 0x04);
        iowrite32((u32)ctx->dma[i].dma_handle, ctx->bar0 + 0x100 + off + 0x00);
    }
    memset(ctx->dma_status_buf, 0, 0x1000);
    // 7: set driver data context and finish:
    pci_set_drvdata(pdev, ctx);
    pci_set_master(pdev);
    printk(KERN_INFO "leechdma: device init: [ %d %08x %08x %08x %08x %08x %08x %08x ]\n", ctx->id,
        (unsigned int)ctx->dma[0].dma_handle,
        (unsigned int)ctx->dma[1].dma_handle,
        (unsigned int)ctx->dma[2].dma_handle,
        (unsigned int)ctx->dma[3].dma_handle,
        (unsigned int)ctx->dma_status_handle,
        (unsigned int)ctx->pa_bar0,
        (unsigned int)ctx->pa_bar1
    );
    return 0;
}

static void leechdma_pci_remove(struct pci_dev *pdev)
{
    int id;
    if(pdev) {
        leechdma_ctx_t *ctx = NULL;
        ctx = pci_get_drvdata(pdev);
        if(ctx->f_active) {
            ctx->f_active = 0;
            while(atomic_read(&ctx->c_user_thread)) {
                fsleep(1);
            }
            id = ctx->id;
            free_irq(pdev->irq, pdev);
            pci_clear_master(pdev);
            fsleep(5);
            memset_io(ctx->bar0 + 0x100, 0, 0x40);
            pci_disable_device(pdev);
            leechdma_ctx_destroy(ctx);
            printk(KERN_INFO "leechdma: device %d removed.\n", id);
        }
    }
}

static struct pci_driver leechdma_pci_driver = {
    .name = LEECHDMA_DRIVER_NAME,
    .id_table = leechdma_pci_tbl,
    .probe = leechdma_pci_probe,
    .remove = leechdma_pci_remove,
};

static int __init leechdma_pci_init(void)
{
    int ret;
    ret = leechdma_chardev_init();
    if(ret) {
        return ret;
    }
    leechdma_devices_active_count = 0;
    memset(leechdma_devices, 0, LEECHDMA_DEVICES_MAX * sizeof(leechdma_ctx_t));
    return pci_register_driver(&leechdma_pci_driver);
}

static void __exit leechdma_pci_exit(void)
{
    int i;
    printk(KERN_INFO "leechdma: driver exit.\n");
    for(i = 0; i < LEECHDMA_DEVICES_MAX; i++) {
        if(leechdma_devices[i].f_active) {
            leechdma_pci_remove(leechdma_devices[i].pdev);
        }
    }
    leechdma_chardev_exit();
    pci_unregister_driver(&leechdma_pci_driver);
}

module_init(leechdma_pci_init);
module_exit(leechdma_pci_exit);

MODULE_AUTHOR("Ulf Frisk");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("LeechDMA driver.");
