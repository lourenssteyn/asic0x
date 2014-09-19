/*
 * iBurst / ArrayComm network driver for USB
 * Copyright (C) 2013 by Lourens Steyn <za.steyn.lourens@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


/*
 * SPONSORSHIP
 *
 * Uniterm Direct <http://www.dbg.co.za>
 * -------------------------------------
 *
 * TP-LINK MR3420 development hardware sponsored by Uniterm Direct <http://www.dbg.co.za>
 *
 */


/*
 * Clean implementation (no status info) for the
 * iBurst / ArrayComm usb modems.
 *
 * Changes: Graham Inggs : module load patch for kernel versions.
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/usb/usbnet.h>

#define UT02_ASIC01_MODEM		0x63
#define UT04_ASIC02_MODEM		0x4d
#define VENDOR_SETUP_REQUEST	0x63


struct ether_packet {
	unsigned char dest[ETH_ALEN];
	unsigned char source[ETH_ALEN];
	unsigned char proto[2];
    unsigned char *payload[0];
};

struct radio_packet {
	unsigned char word[2];
	unsigned char packet;
	unsigned char check;
	unsigned char proto[2];
    unsigned char *payload[0];
};

static int asic0x_rx_fixup(struct usbnet *dev, struct sk_buff *skb) {

	struct radio_packet *usbradio_packet = (struct radio_packet*)skb->data;

	struct ether_packet *ethernet_packet = (struct ether_packet*)0;

    bool is_broadcast = false;

	if (pskb_expand_head(skb, 8, 0, GFP_ATOMIC) != 0) {
		netdev_dbg(dev->net, "%s unable to expand skb for fixup.\n", __func__);
		return 0;
	}

    if (skb->len < sizeof(struct ether_packet)) { /* don't process short packets */
        return 0;
    }

    if (usbradio_packet->word[0] & 0x08) {
        is_broadcast = true;
    }

    ethernet_packet = (struct ether_packet*)skb_push(skb, 8);
    
    memcpy(ethernet_packet->source, dev->net->dev_addr, ETH_ALEN);

    if (is_broadcast) {
		memset(ethernet_packet->dest, 0xff, ETH_ALEN);
    }
	else {
		ethernet_packet->source[ETH_ALEN - 1] ^= 1;
		memcpy(ethernet_packet->dest, dev->net->dev_addr, ETH_ALEN);
	}
        
    return 1;
}

static struct sk_buff *asic0x_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags) {
    
    struct ether_packet *ethernet_packet = (struct ether_packet*)skb->data;
	
    struct radio_packet *usbradio_packet = (struct radio_packet*)0L;
    
    int payload_length, packet_length;
        
    payload_length = skb->len - sizeof(struct ether_packet);
    
    packet_length = payload_length + sizeof(struct radio_packet);
    
    usbradio_packet = (struct radio_packet *)skb_pull(skb, 8);
        
	if (ethernet_packet->dest[0] & 1) {
		usbradio_packet->word[0] = (packet_length >> 8) | 0x08;
	}
	else {
		usbradio_packet->word[0] = packet_length >> 8;
	}
	
	usbradio_packet->word[1] = packet_length & 0xff;
	
	usbradio_packet->check = (packet_length & 0xff) ^ 0xff;

    usbradio_packet->packet = 0;

    return skb;
}

static int asic0x_bind(struct usbnet *dev, struct usb_interface *intf) {

	int status, actual_len;

	u8 *setup_data, modem_type;
	
	u8 config_data[8] = {0x0, 0x8, 0x0, 0xf7, 0xac, 0x3, 0x0, 0x2};
	
    status = usb_reset_configuration(dev->udev);
    
    if (status < 0) {
        dev_err(&dev->udev->dev, "%s unable to reset usb configuration.\n", __func__);
        goto err_end;
    }

	status = usbnet_get_endpoints(dev, intf);

    if (status < 0) {
        dev_err(&dev->udev->dev, "%s unable to get device endpoints.\n", __func__);
		goto err_end;
	}

	modem_type = 0;

	setup_data = kmalloc(16, GFP_ATOMIC);

	status = usb_control_msg(
				dev->udev,
				usb_rcvctrlpipe(dev->udev, 0),
			    VENDOR_SETUP_REQUEST,
			    USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			    0,
			    0,
			    setup_data,
			    8,
			    USB_CTRL_GET_TIMEOUT);


	if (status > 0) {
		modem_type = setup_data[1];
		memcpy(dev->net->dev_addr, &setup_data[2], ETH_ALEN);
	}

	kfree(setup_data);

	if (status != 8) {
        dev_err(&dev->udev->dev, "%s unable to read vendor setup data.\n", __func__);
        goto err_end;
	}	

	switch (modem_type) {
		case UT02_ASIC01_MODEM:
			dev_info(&dev->udev->dev, "%s UT02_ASIC01_MODEM type detected.\n", __func__);
			break;
			
		case UT04_ASIC02_MODEM:
			dev_info(&dev->udev->dev, "%s UT04_ASIC02_MODEM type detected.\n", __func__);
			break;
			
		default:
			dev_err(&dev->udev->dev, "%s invalid modem type %02x detected.\n", __func__, modem_type);
            status = -EIO;
            goto err_end;
	}

	dev->net->dev_addr[0] &= ~1;
	
    dev->net->flags = IFF_BROADCAST | IFF_DYNAMIC | IFF_NOARP;

	setup_data = kmalloc(16, GFP_ATOMIC);

	memcpy(setup_data, config_data, 8);

    status = usb_bulk_msg(
                dev->udev,
                dev->out,
                setup_data,
                8,
                &actual_len,
                0);

	kfree(setup_data);
    
    if (status < 0) {
        dev_err(&dev->udev->dev, "%s unable to send configuration data.\n", __func__);
        goto err_end;
    }
    
    if (actual_len < 8) {
        dev_err(&dev->udev->dev, "%s incomplete configuration data sent, count = %d\n", __func__, actual_len);
        goto err_end;
    }

    return 0;

err_end:
	usb_set_intfdata(intf, NULL);
	usb_driver_release_interface(driver_of(intf), intf);

    return status;
}

static void asic0x_status(struct usbnet *dev, struct urb *urb) {
    netif_carrier_on(dev->net);
}

static const struct driver_info asic0x_info = {
	.description	= "iBurst Terminal",
	.flags		    = FLAG_ETHER | FLAG_RX_ASSEMBLE,
	.bind		    = asic0x_bind,
	.rx_fixup	    = asic0x_rx_fixup,
	.tx_fixup	    = asic0x_tx_fixup,
	.status		    = asic0x_status,
};

static const struct usb_device_id products[] = {
	{
        USB_DEVICE(0x0482, 0x0204),
        .driver_info = (unsigned long)&asic0x_info,
    },
    { },
};

MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver asic0x_driver = {
	.name       = "asic0x",
	.id_table   = products,
	.probe      = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend    = usbnet_suspend,
	.resume     = usbnet_resume,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
 	.disable_hub_initiated_lpm = 1,
#endif
};

/*
 * Module load patch for kernel versions.
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,3,0)
static int __init asic0x_init(void)
{
	return usb_register(&asic0x_driver);
}
module_init(asic0x_init);

static void __exit asic0x_exit(void)
{
	usb_deregister(&asic0x_driver);
}
module_exit(asic0x_exit);
#else
module_usb_driver(asic0x_driver);
#endif

MODULE_AUTHOR("Lourens Steyn <za.steyn.lourens@gmail.com>");
MODULE_DESCRIPTION("ArrayComm ASIC01/ASIC02 iBurst devices");
MODULE_LICENSE("GPL");
