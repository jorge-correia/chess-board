#include <asm/uaccess.h> /* put_user */
#include <linux/cdev.h> /* cdev_ */
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include "chess_board_driver.h"

static dev_t first_dev;
static struct class *chess_devclass;
static struct cdev chess_chrdev;


//static int chess_irq;
static struct pci_dev *chess_pcidev;
static void __iomem *mmio;

void *virt_buffer;

/*
in real world, this address would be the a bus address (IOVA translated by IOMMU
or an address that will suffer an offset from host bridge. In qemu the current
qemu config, this address is the same as host address. I need to understand the
scenario where IOVA is enabled
*/
dma_addr_t phys_buffer; 

static struct pci_device_id pci_ids[] = {
	//creating a struct pci_device_id matching the vendor id and device id.
	{ PCI_DEVICE(QEMU_VENDOR_ID, CHESS_BOARD_DEV_ID), },
	{ 0, }
};

//needed to allow hotplug and inform kernel what module works with this hardware
MODULE_DEVICE_TABLE (pci, pci_ids);

static long chess_ioctl (struct file *file, unsigned int cmd, unsigned long arg)
{

        //uint32_t __user *usr = (int*) arg;
        //uint32_t reg_value;
        switch (cmd) {
                case CHESS_IOCTL_READ_REG:
                        printk ("[CHESS-driver] starting DMA (host -> dev)\n");
                        
                        //memset (virt_buffer, 'c', 0x1000);

                        // setting addresses
                        // src low
                        iowrite32 (((uint64_t)phys_buffer) & 0xffffffff,
                                        ((uint8_t*)mmio) + 0x4);
                        // src high
                        iowrite32 ((((uint64_t)phys_buffer) >> 32) &
                                        0xffffffff, ((uint8_t*)mmio) + 0x8);
                        // dst low
                        iowrite32 (0 ,((uint8_t*)mmio) + 0xc);
                        // dst high
                        iowrite32 (0 ,((uint8_t*)mmio) + 0x10);
                        // size
                        iowrite32 (0x1000,((uint8_t*)mmio) + 0x14);

                        // setting command to READ
                        iowrite32 (1, (void*)mmio);
                break;

                case CHESS_IOCTL_WRITE_REG:
                        printk ("[CHESS-driver] starting DMA (dev -> host)\n");
                        // never gonna read Z
                        memset (virt_buffer, 'z', 0x1000);

                        // setting addresses
                        // src low
                        iowrite32 (0 ,((uint8_t*)mmio) + 0x4);
                        // src high
                        iowrite32 (0 ,((uint8_t*)mmio) + 0x8);
                        // dst low
                        iowrite32 (((uint64_t)phys_buffer) & 0xffffffff,
                                        ((uint8_t*)mmio) + 0xc);
                        // dst high
                        iowrite32 ((((uint64_t)phys_buffer) >> 32) &
                                        0xffffffff, ((uint8_t*)mmio) + 0x10);
                        // size
                        iowrite32 (0x1000,((uint8_t*)mmio) + 0x14);

                        // setting command to WRITE
                        iowrite32 (2, (void*)mmio);
                break;

                default:
                        printk ("[CHESS-driver] unknown IOCTL command %d\n",
                                        cmd);
        }

        return 0;
}

static unsigned int chess_mmap_fault_handler (struct vm_fault *vmf)
{
        printk ("[CHESS-driver] starting mmap fault %llx\n", vmf->pgoff);
        struct page *page = virt_to_page (virt_buffer +
                        (vmf->pgoff * PAGE_SIZE));

        get_page (page);
        vmf->page = page;

        return 0;
}

struct vm_operations_struct chess_vm_ops = {
        .fault = chess_mmap_fault_handler,
};

static int chess_mmap (struct file *filp, struct vm_area_struct *vma)
{
        vma->vm_ops = &chess_vm_ops;
        return 0;
}


static struct file_operations chrdev_fops = {
        .owner = THIS_MODULE,
        .unlocked_ioctl = chess_ioctl,
        .mmap = chess_mmap,
};

/*
        PROBABLY THIS IRQ CAN BE SHARED AMONG OTHER DEVICES. THIS IS WHY I NEED
        TO VERIFY IF THE DEVICE INTERRUPT BIT IS SET (I am not doing this)
*/
static irqreturn_t chess_irq_handler (int irq, void *dev)
{
        /*
        int rec_major = *(int*)dev;
        if (rec_major != first_dev)
                return IRQ_NONE;
         */
        printk ("[CHESS-driver] IRQ received on device %d\n", *(int*)dev);
        uint32_t interrupt_status = ioread32 (((uint8_t*)(mmio )) + 0x18);
        printk ("[CHESS-driver] IRQ cause %d\n", interrupt_status);
        
        printk ("[CHESS-driver] dma bytes:\n");
        
        if (interrupt_status == 2)
                for (int i = 0 ; i < 0x10; i++)
                        printk ("%c\n", *(uint8_t*)(((uint8_t*)virt_buffer) + i));
       
       

        return IRQ_HANDLED;

        
}

static irqreturn_t chess_msi_handler (int irq, void *dev)
{
        printk ("[CHESS-driver] MSI received on device %d\n", *(int*)dev);
        uint32_t interrupt_status = ioread32 (((uint8_t*)(mmio )) + 0x18);
        printk ("[CHESS-driver] MSI cause %d\n", interrupt_status);

        printk ("[CHESS-driver] dma bytes:\n");
        
        if (interrupt_status == 2)
                for (int i = 0 ; i < 0x10; i++)
                        printk ("%c\n", *(uint8_t*)(((uint8_t*)virt_buffer) + i));
        return IRQ_HANDLED;

        
}


static int chess_probe (struct pci_dev *dev, const struct pci_device_id *id)
{
        int err;

        // creating chrdev file to interface with userspace
        if (alloc_chrdev_region (&first_dev, 0, 1,
                                CHESS_BOARD_DEVREGION_NAME) < 0) {

                printk ("[CHESS-driver] error on alloc_chrdev_region\n");
                goto error;
        }
        
        if (IS_ERR(chess_devclass = class_create(CHESS_BOARD_DEVCLASS_NAME))) {
                unregister_chrdev_region(first_dev, 1);
                printk ("[CHESS-driver] error on class_create\n");
                goto error;
        }

        if (IS_ERR(device_create(chess_devclass, 0, first_dev, 0,
                                        CHESS_BOARD_CHRDEV_NAME))) {

                class_destroy(chess_devclass);
                unregister_chrdev_region(first_dev, 1);
                printk ("[CHESS-driver] error on device_create\n");
                goto error;
        }

        cdev_init (&chess_chrdev, &chrdev_fops);
        if ((cdev_add(&chess_chrdev, first_dev, 1)) < 0)
        {
                device_destroy(chess_devclass, first_dev);
                class_destroy(chess_devclass);
                unregister_chrdev_region(first_dev, 1);
                printk ("[CHESS-driver] error on cdev_add\n");
                goto error;
        }

        printk ("[CHESS-driver] first_dev: %d\n", first_dev);


        chess_pcidev = dev;

        // informing OS that our device will use 32bit address
        err = dma_set_mask_and_coherent(&dev->dev, DMA_BIT_MASK(32));
        if (err) {
                printk ("[CHESS-driver] error on set DMA mask\n");
                goto error;
        }




        if (pci_enable_device (dev) < 0){
                printk ("[CHESS-driver] error on enabling PCI device\n");
                goto error;
        }

        pci_set_master (dev);

        /* just making sure that no other drivers will use the PCI address
        space of our device. At this point, BAR is already setted with an 
        physical address.
        */
        if (pci_request_region (dev, CHESS_BAR, "chess-bar0")) {
                printk ("[CHESS-driver] error on requesting BAR access\n");
                goto error;
        }

        // sanity checking BAR type
        if ((pci_resource_flags(dev, CHESS_BAR) & IORESOURCE_MEM) !=
                        IORESOURCE_MEM) {

                printk ("[CHESS-driver] Unexpected BAR type\n");
                goto error;
        }

        /* getting a virtual memory address that points to the "physical memory"
        present in BAR
        */
        mmio = pci_iomap (dev, CHESS_BAR, pci_resource_len (dev, CHESS_BAR));


        // printing some infos
        resource_size_t start = pci_resource_start (dev, CHESS_BAR);
        resource_size_t end = pci_resource_end (dev, CHESS_BAR);
        printk ("[CHESS-driver] BAR start: %llx | BAR end: %llx | size: %llx\n",
                        (uint64_t) start, (uint64_t) end,
                        (uint64_t) (end + 1 - start));

        printk ("[CHESS-driver] sizeof(resource_size_t): %lx\n",
                        sizeof(resource_size_t));
        
        
        printk ("[CHESS-driver] initial reg value: %x\n",
                        ioread32 ((void*)mmio));

        /*
           adding handler for interrupt line

           the return of pci_alloc_irq_vectors is positive if the PCI device
           is MSI-capable (executed msi_init on realize)
         */
        int num_vectors = pci_alloc_irq_vectors(dev, 1, 1, PCI_IRQ_MSI);
        printk ("[CHESS-driver] num_vectors: %d\n", num_vectors);

        if (num_vectors < 0)
        {
                // fallback to INTx
                printk ("[CHESS-driver] registering INTx handler\n");
                if (request_irq (pci_irq_vector(dev, 0), chess_irq_handler,
                                        IRQF_SHARED, "chess-board",
                                        &first_dev) < 0){
                        printk ("[CHESS-driver] error registering IRQ handler\n");
                        goto error;
                }
        }
        else {

                printk ("[CHESS-driver] registering MSI handler %d\n",
                                pci_irq_vector (dev, 0));

                if (request_irq(pci_irq_vector(dev, 0), chess_msi_handler, 0,
                                        "chess-board", &first_dev)) {
                        printk ("[CHESS-driver] error registering MSI handler\n");
                        goto error;
                }
        }
        printk ("[CHESS-driver] done with INTx/MSI handler\n");


        // allocating region for DMA
        virt_buffer = dma_alloc_coherent (&dev->dev, 0x1000, &phys_buffer,
                        GFP_KERNEL);


        /*
           configuring MSI interrupt to generate NMI. The steps are:
           1. find MSI capability struct
           2. Message data field is composed of 4 fields. We need to set the 
           field "delivery mode" (10:8) to NMI and "trigger mode"(12) to zero 
           (indicating edge trigger mode)

           The PCI device creates the MSI message based on this message data
           field
         */

        /*
        uint16_t msi_cap_offset = pci_find_capability (dev, PCI_CAP_ID_MSI);
        uint16_t msg_data;
        pci_read_config_word (dev, msi_cap_offset + PCI_MSI_DATA_64, &msg_data);
       
        msg_data &= ~0x0700;
        msg_data |= 0x0400;
        msg_data &= ~(1 << 12);
        printk ("writing capability value %x\n", msg_data);
        pci_write_config_word (dev, msi_cap_offset + PCI_MSI_DATA_64, msg_data);
        */

        
        printk ("[CHESS-driver] chess_probe() ended successfully\n");
        return 0;
error:
        return 1;

}



static void chess_remove (struct pci_dev *dev)
{
        printk ("[CHESS-driver] removing chess PCI\n");
        // TODO clear irq
        /*
          free_irq(pci_irq_vector(pdev, 0), my_data);
          pci_free_irq_vectors(pdev);
        */

        pci_release_region (dev, CHESS_BAR);

        cdev_del(&chess_chrdev);
        device_destroy(chess_devclass, first_dev);
        class_destroy(chess_devclass);
        unregister_chrdev_region(first_dev, 1);


}

static struct pci_driver chess_driver = {
        .name = "chess-driver",
        .id_table = pci_ids,
        .probe = chess_probe, // called after module init if device is connected
        .remove = chess_remove,

};

static int __init chess_init (void)
{
        if (pci_register_driver (&chess_driver) < 0){
                printk ("[CHESS-driver] Error loading PCI driver");
                return 1;
        }
        printk ("[CHESS-driver] PCI driver loaded!");
        return 0;
}

static void __exit chess_exit (void)
{
        printk ("[CHESS-driver] unoading chess driver!");
        cdev_del(&chess_chrdev);
        device_destroy(chess_devclass, first_dev);
        class_destroy(chess_devclass);
        unregister_chrdev_region(first_dev, 1);

        pci_unregister_driver (&chess_driver);
}

MODULE_LICENSE ("GPL");
module_init (chess_init);
module_exit (chess_exit);
