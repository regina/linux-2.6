/*
 * OHCI HCD (Host Controller Driver) for USB.
 *
 * (C) Copyright 1999 Roman Weissgaerber <weissg@vienna.at>
 * (C) Copyright 2000-2002 David Brownell <dbrownell@users.sourceforge.net>
 * (C) Copyright 2002 Hewlett-Packard Company
 * (C) Copyright 2005 Embedded Alley Solutions, Inc.
 *
 * Bus Glue for PNX8550
 *
 * Written by Christopher Hoover <ch@hpl.hp.com>
 * Based on fragments of previous driver by Russell King et al.
 *
 * Modified for LH7A404 from ohci-sa1111.c
 *  by Durgesh Pattamatta <pattamattad@sharpsec.com>
 *
 * Modified for pxa27x from ohci-lh7a404.c
 *  by Nick Bane <nick@cecomputing.co.uk> 26-8-2004
 *
 * Modified for PNX8550 from ohci-pxa27x.c
 *  by Embedded Alley Solutions, Inc.
 *
 * This file is licenced under the GPL.
 */

#include <linux/device.h>
#include <asm/mach-pnx8550/usb.h>
#include <asm/mach-pnx8550/int.h>
#include <asm/mach-pnx8550/pci.h>

#ifndef CONFIG_PNX8550
#error "This file is PNX8550 bus glue.  CONFIG_PNX8550 must be defined."
#endif

extern int usb_disabled(void);

/*-------------------------------------------------------------------------*/

static void pnx8550_start_hc(struct platform_device *dev)
{
	/*
	 * Set register CLK48CTL to enable and 48MHz
	 */
	outl(0x00000003, PCI_BASE | 0x0004770c);

	/*
	 * Set register CLK12CTL to enable and 48MHz
	 */
	outl(0x00000003, PCI_BASE | 0x00047710);

	udelay(100);
}

static void pnx8550_stop_hc(struct platform_device *dev)
{
	udelay(10);
}


/*-------------------------------------------------------------------------*/

/* configure so an HC device and id are always provided */
/* always called with process context; sleeping is OK */

/**
 * usb_hcd_pnx8550_remove - shutdown processing for pnx8550-based HCDs
 * @dev: USB Host Controller being removed
 * Context: !in_interrupt()
 *
 * Reverses the effect of usb_hcd_pnx8550_probe(), first invoking
 * the HCD's stop() method.  It is always called from a thread
 * context, normally "rmmod", "apmd", or something similar.
 *
 */
void usb_hcd_pnx8550_remove (struct usb_hcd *hcd, struct platform_device *dev)
{
	pr_debug ("remove: %s, state %x", hcd->self.bus_name, hcd->state);

	if (in_interrupt ())
		BUG ();

	hcd->state = USB_STATE_QUIESCING;

	pr_debug ("%s: roothub graceful disconnect", hcd->self.bus_name);
	usb_disconnect (&hcd->self.root_hub);

	hcd->driver->stop (hcd);
	hcd->state = USB_STATE_HALT;

	free_irq (hcd->irq, hcd);
	hcd_buffer_destroy (hcd);

	usb_deregister_bus (&hcd->self);

	pnx8550_stop_hc(dev);

	iounmap(hcd->regs);
	release_mem_region(pci_resource_start (dev, 0),
				pci_resource_len (dev, 0));
}

static irqreturn_t usb_hcd_pnx8550_hcim_irq (int irq, void *__hcd,
					     struct pt_regs * r)
{
	struct usb_hcd *hcd = __hcd;

	return usb_hcd_irq(irq, hcd, r);
}

/**
 * usb_hcd_pnx8550_probe - initialize pnx8550-based HCDs
 * Context: !in_interrupt()
 *
 * Allocates basic resources for this USB host controller, and
 * then invokes the start() method for the HCD associated with it
 * through the hotplug entry's driver_data.
 *
 */

int usb_hcd_pnx8550_probe (const struct hc_driver *driver,
			  struct usb_hcd **hcd_out,
			  struct platform_device *dev)
{
	int retval;
	struct usb_hcd *hcd = 0;

	unsigned int *addr = NULL;

	if (!request_mem_region(dev->resource[0].start,
				dev->resource[0].end
				- dev->resource[0].start + 1, hcd_name)) {
		pr_debug("request_mem_region failed");
		return -EBUSY;
	}

	pnx8550_start_hc(dev);

	addr = ioremap(dev->resource[0].start,
		       dev->resource[0].end
		       - dev->resource[0].start + 1);
	if (!addr) {
		pr_debug("ioremap failed");
		retval = -ENOMEM;
		goto err1;
	}

	hcd = ohci_hcd_alloc();
	if (hcd == NULL){
		pr_debug ("hcd_alloc failed");
		retval = -ENOMEM;
		goto err1;
	}

	if(dev->resource[1].flags != IORESOURCE_IRQ){
		pr_debug ("resource[1] is not IORESOURCE_IRQ");
		retval = -ENOMEM;
		goto err1;
	}

	hcd->driver = (struct hc_driver *) driver;
	hcd->description = driver->description;
	hcd->irq = dev->resource[1].start;
	hcd->regs = addr;
	hcd->self.controller = &dev->dev;

	retval = hcd_buffer_create (hcd);
	if (retval != 0) {
		pr_debug ("pool alloc fail");
		goto err1;
	}

	retval = request_irq (hcd->irq, usb_hcd_pnx8550_hcim_irq, SA_INTERRUPT,
			      hcd->description, hcd);
	if (retval != 0) {
		pr_debug("request_irq failed");
		retval = -EBUSY;
		goto err2;
	}

	pr_debug ("%s (PNX8550 OHCI) at 0x%p, irq %d",
	     hcd->description, hcd->regs, hcd->irq);

	usb_bus_init (&hcd->self);
	hcd->self.op = &usb_hcd_operations;
	hcd->self.hcpriv = (void *) hcd;
	hcd->self.bus_name = "pnx8550";
	hcd->product_desc = "PNX8550 OHCI";

	INIT_LIST_HEAD (&hcd->dev_list);

	usb_register_bus (&hcd->self);

	if ((retval = driver->start (hcd)) < 0)
	{
		usb_hcd_pnx8550_remove(hcd, dev);
		printk("bad driver->start\n");
		return retval;
	}

	*hcd_out = hcd;
	return 0;

 err2:
	hcd_buffer_destroy (hcd);
 err1:
	kfree(hcd);
	pnx8550_stop_hc(dev);
	release_mem_region(dev->resource[0].start,
				dev->resource[0].end
			   - dev->resource[0].start + 1);
	return retval;
}

/* may be called without controller electrically present */
/* may be called with controller, bus, and devices active */

/*-------------------------------------------------------------------------*/

static int __devinit
ohci_pnx8550_start (struct usb_hcd *hcd)
{
	struct ohci_hcd	*ohci = hcd_to_ohci (hcd);
	int		ret;

	ohci_dbg (ohci, "ohci_pnx8550_start, ohci:%p", ohci);

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run (ohci)) < 0) {
		err ("can't start %s", hcd->self.bus_name);
		ohci_stop (hcd);
		return ret;
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static const struct hc_driver ohci_pnx8550_hc_driver = {
	.description =		hcd_name,

	/*
	 * generic hardware linkage
	 */
	.irq =			ohci_irq,
	.flags =		HCD_USB11 | HCD_MEMORY,

	/*
	 * basic lifecycle operations
	 */
	.start =		ohci_pnx8550_start,
	.stop =			ohci_stop,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		ohci_urb_enqueue,
	.urb_dequeue =		ohci_urb_dequeue,
	.endpoint_disable =	ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number =	ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	ohci_hub_status_data,
	.hub_control =		ohci_hub_control,
#ifdef  CONFIG_USB_SUSPEND
	.hub_suspend =		ohci_hub_suspend,
	.hub_resume =		ohci_hub_resume,
#endif
};

/*-------------------------------------------------------------------------*/

static int ohci_hcd_pnx8550_drv_probe(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = NULL;
	int ret;

	pr_debug ("In ohci_hcd_pnx8550_drv_probe");

	if (usb_disabled())
		return -ENODEV;

	ret = usb_hcd_pnx8550_probe(&ohci_pnx8550_hc_driver, &hcd, pdev);

	if (ret == 0)
		dev_set_drvdata(dev, hcd);

	return ret;
}

static int ohci_hcd_pnx8550_drv_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct usb_hcd *hcd = dev_get_drvdata(dev);

	usb_hcd_pnx8550_remove(hcd, pdev);
	return 0;
}

static int ohci_hcd_pnx8550_drv_suspend(struct device *dev, u32 state, u32 level)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct usb_hcd *hcd = dev_get_drvdata(dev);
	printk("%s: not implemented yet\n", __FUNCTION__);

	return 0;
}

static int ohci_hcd_pnx8550_drv_resume(struct device *dev, u32 state)
{
//	struct platform_device *pdev = to_platform_device(dev);
//	struct usb_hcd *hcd = dev_get_drvdata(dev);
	printk("%s: not implemented yet\n", __FUNCTION__);

	return 0;
}


static struct device_driver ohci_hcd_pnx8550_driver = {
	.name		= "pnx8550-ohci",
	.bus		= &platform_bus_type,
	.probe		= ohci_hcd_pnx8550_drv_probe,
	.remove		= ohci_hcd_pnx8550_drv_remove,
	.suspend	= ohci_hcd_pnx8550_drv_suspend,
	.resume		= ohci_hcd_pnx8550_drv_resume,
};

static int __init ohci_hcd_pnx8550_init (void)
{
	pr_debug (DRIVER_INFO " (pnx8550)");
	pr_debug ("block sizes: ed %d td %d\n",
		sizeof (struct ed), sizeof (struct td));

	return driver_register(&ohci_hcd_pnx8550_driver);
}

static void __exit ohci_hcd_pnx8550_cleanup (void)
{
	driver_unregister(&ohci_hcd_pnx8550_driver);
}

module_init (ohci_hcd_pnx8550_init);
module_exit (ohci_hcd_pnx8550_cleanup);
