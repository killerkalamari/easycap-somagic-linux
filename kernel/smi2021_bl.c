/*******************************************************************************
 * smi2021_bl.c	 	                                                       *
 *                                                                             *
 * USB Driver for SMI2021 - EasyCAP                                            *
 * USB ID 1c88:003c                                                            *
 *                                                                             *
 * *****************************************************************************
 *
 * Copyright 2011-2013 Jon Arne Jørgensen
 * <jonjon.arnearne--a.t--gmail.com>
 *
 * Copyright 2011, 2012 Tony Brown, Michal Demin, Jeffry Johnston
 *
 * This file is part of SMI2021
 * http://code.google.com/p/easycap-somagic-linux/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * This driver is heavily influensed by the STK1160 driver.
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 */

#include <linux/module.h>
#include <linux/usb.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#define FIRMWARE_CHUNK_SIZE		62
#define FIRMWARE_HEADER_SIZE		2

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jon Arne Jørgensen <jonjon.arnearne--a.t--gmail.com>");
MODULE_DESCRIPTION("SMI2021 - Bootloader");
MODULE_VERSION("0.1");

static unsigned int firmware_version = 0;
module_param(firmware_version, int, 0644);
MODULE_PARM_DESC(firmware_version,
			"Firmware version to be uploaded to device \n"
			"if there are more than one version present");

struct usb_device_id smi2021_bootloader_id_table[] = {
	{ USB_DEVICE(0x1c88, 0x0007) },
	{ }
};

struct smi2021_firmware {
	int		id;
	const char	*name;
	int		found;
};

struct smi2021_firmware available_fw[] = {
	{
		.id = 0x3c,
		.name = "smi2021_3c.bin",
	},
	{
		.id = 0x3e,
		.name = "smi2021_3e.bin",
	},
	{
		.id = 0x3f,
		.name = "smi2021_3f.bin",
	}
};

static const struct firmware *firmware[ARRAY_SIZE(available_fw)];
static int firmwares = -1;

static int smi2021_load_firmware(struct usb_device *udev,
					const struct firmware *firmware)
{
	int i, size, rc = 0;
	u8 *chunk;
	u16 ack = 0x0000;

	if (udev == NULL) {
		goto end_out;
	}

	size = FIRMWARE_CHUNK_SIZE + FIRMWARE_HEADER_SIZE;
	chunk = kzalloc(size, GFP_KERNEL);
	chunk[0] = 0x05;
	chunk[1] = 0xff;

	if (chunk == NULL) {
		dev_err(&udev->dev,
			"could not allocate space for firmware chunk\n");
		goto end_out;
	}

	if (firmware == NULL) {
		dev_err(&udev->dev, "firmware is NULL\n");
		rc = -ENODEV;
		goto free_out;
	}

	if (firmware->size % FIRMWARE_CHUNK_SIZE) {
		dev_err(&udev->dev, "firmware has wrong size\n");
		rc = -ENODEV;
		goto free_out;
	}

	rc = usb_control_msg(udev, usb_rcvctrlpipe(udev, 0x80), 0x01,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x0001, 0x0000, &ack, sizeof(ack), 1000);

	if (rc < 0) {
		dev_err(&udev->dev, "could not prepare device for upload: %d\n",
			rc);
		goto free_out;
	}
	if (__cpu_to_be16(ack) != 0x0107) {
		dev_err(&udev->dev, "could not prepare device for upload: %d\n",
			rc);
		goto free_out;
	}

	for (i = 0; i < firmware->size / FIRMWARE_CHUNK_SIZE; i++) {
		memcpy(chunk + FIRMWARE_HEADER_SIZE,
			firmware->data + (i * FIRMWARE_CHUNK_SIZE),
			FIRMWARE_CHUNK_SIZE);

		rc = usb_control_msg(udev, usb_sndctrlpipe(udev, 0x00), 0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x0005, 0x0000, chunk, size, 1000);
		if (rc < 0) {
			dev_err(&udev->dev, "firmware upload failed: %d\n",
				rc);
			goto free_out;
		}
	}

	ack = __cpu_to_le16(0x0007);
	rc = usb_control_msg(udev, usb_sndctrlpipe(udev, 0x00), 0x01,
			USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			0x0007, 0x0000, &ack, sizeof(ack), 1000);

	if (rc < 0) {
		dev_err(&udev->dev, "device failed to ack firmware: %d\n", rc);
		goto free_out;
	}

	rc = 0;

free_out:
	kfree(chunk);	
end_out:
	return rc;
}

static int smi2021_choose_firmware(struct usb_device *udev)
{
	int i, found, id;
	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		found = available_fw[i].found;
		id = available_fw[i].id;
		if (firmware_version == id && found >= 0) {
			dev_info(&udev->dev, "uploading firmware for 0x%x\n",
					id);
			return smi2021_load_firmware(udev, firmware[found]);
		}
	}

	dev_info(&udev->dev,
	"could not decide what firmware to upload, user action required\n");
	return 0;
}

static int __devinit smi2021_bootloader_probe(struct usb_interface *intf,
					const struct usb_device_id *devid)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int rc, i;

	/* Check what firmwares are available in the system */
	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		dev_info(&udev->dev, "Looking for: %s\n", available_fw[i].name);
		rc = request_firmware(&firmware[firmwares + 1],
			available_fw[i].name, &udev->dev);

		if (rc == 0) {
			firmwares++;
			available_fw[i].found = firmwares;
			dev_info(&udev->dev, "Found firmware for 0x00%x\n",
				available_fw[i].id);
		} else if (rc == -ENOENT) {
			available_fw[i].found = -1;
		} else {
			dev_err(&udev->dev,
				"request_firmware failed with: %d\n", rc);
			goto err_out;
		}
	}

	if (firmwares < 0) {
		dev_err(&udev->dev,
			"could not find any firmware for this device\n");
		goto no_dev;
	} else if (firmwares == 0) {
		rc = smi2021_load_firmware(udev, firmware[0]);
		if (rc < 0) {
			goto err_out;
		}
	} else {
		smi2021_choose_firmware(udev);
	}

	return 0;

no_dev:
	rc = -ENODEV;
err_out:
	return rc;
	
}

static void __devexit smi2021_bootloader_disconnect(struct usb_interface *intf)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int i;

	for (i = 0; i < ARRAY_SIZE(available_fw); i++) {
		if (available_fw[i].found >= 0) {
			dev_info(&udev->dev, "Releasing firmware for 0x00%x\n",
							available_fw[i].id);
			release_firmware(firmware[available_fw[i].found]);
			firmware[available_fw[i].found] = NULL;
			available_fw[i].found = -1;
		}
	}
	firmwares = -1;

}

struct usb_driver smi2021_bootloader_driver = {
	.name		= "smi2021_bootloader",
	.id_table	= smi2021_bootloader_id_table,
	.probe		= smi2021_bootloader_probe,
	.disconnect	= smi2021_bootloader_disconnect
};

module_usb_driver(smi2021_bootloader_driver);
