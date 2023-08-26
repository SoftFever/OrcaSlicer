/*
 * avrdude - A Downloader/Uploader for AVR device programmers
 * Copyright (C) 2003-2004  Theodore A. Roth  <troth@openavr.org>
 * Copyright (C) 2006 Joerg Wunsch <j@uriah.heep.sax.de>
 * Copyright (C) 2006 Christian Starkjohann
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/* $Id$ */

/*
 * Serial Interface emulation for USB programmer "AVR-Doper" in HID mode.
 */

#include "ac_cfg.h"

#if defined(HAVE_LIBUSB) || (defined(WIN32NATIVE) && defined(HAVE_LIBHID))

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "avrdude.h"
#include "libavrdude.h"

/* ------------------------------------------------------------------------ */

/* Numeric constants for 'reportType' parameters */
#define USB_HID_REPORT_TYPE_INPUT   1
#define USB_HID_REPORT_TYPE_OUTPUT  2
#define USB_HID_REPORT_TYPE_FEATURE 3

/* These are the error codes which can be returned by functions of this
 * module.
 */
#define USB_ERROR_NONE      0
#define USB_ERROR_ACCESS    1
#define USB_ERROR_NOTFOUND  2
#define USB_ERROR_BUSY      16
#define USB_ERROR_IO        5

#define USB_VENDOR_ID   0x16c0
#define USB_PRODUCT_ID  0x05df

static int  reportDataSizes[4] = {13, 29, 61, 125};

static unsigned char    avrdoperRxBuffer[280];  /* buffer for receive data */
static int              avrdoperRxLength = 0;   /* amount of valid bytes in rx buffer */
static int              avrdoperRxPosition = 0; /* amount of bytes already consumed in rx buffer */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if defined(WIN32NATIVE) && defined(HAVE_LIBHID)

#include <windows.h>
#include <setupapi.h>

#if defined(HAVE_DDK_HIDSDI_H)
#  include <ddk/hidsdi.h>
#else
#  include "my_ddk_hidsdi.h"
#endif
#include <ddk/hidpi.h>

#ifdef USB_DEBUG
#define DEBUG_PRINT(arg)    printf arg
#else
#define DEBUG_PRINT(arg)
#endif

/* ------------------------------------------------------------------------ */

static void convertUniToAscii(char *buffer)
{
    unsigned short  *uni = (void *)buffer;
    char            *ascii = buffer;

    while(*uni != 0){
        if(*uni >= 256){
            *ascii++ = '?';
            uni++;
        }else{
            *ascii++ = *uni++;
        }
    }
    *ascii++ = 0;
}

static int usbOpenDevice(union filedescriptor *fdp, int vendor, char *vendorName,
			 int product, char *productName, int usesReportIDs)
{
    GUID                                hidGuid;        /* GUID for HID driver */
    HDEVINFO                            deviceInfoList;
    SP_DEVICE_INTERFACE_DATA            deviceInfo;
    SP_DEVICE_INTERFACE_DETAIL_DATA     *deviceDetails = NULL;
    DWORD                               size;
    int                                 i, openFlag = 0;  /* may be FILE_FLAG_OVERLAPPED */
    int                                 errorCode = USB_ERROR_NOTFOUND;
    HANDLE                              handle = INVALID_HANDLE_VALUE;
    HIDD_ATTRIBUTES                     deviceAttributes;

    HidD_GetHidGuid(&hidGuid);
    deviceInfoList = SetupDiGetClassDevs(&hidGuid, NULL, NULL,
					 DIGCF_PRESENT | DIGCF_INTERFACEDEVICE);
    deviceInfo.cbSize = sizeof(deviceInfo);
    for(i=0;;i++){
        if(handle != INVALID_HANDLE_VALUE){
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
        if(!SetupDiEnumDeviceInterfaces(deviceInfoList, 0, &hidGuid, i, &deviceInfo))
            break;  /* no more entries */
        /* first do a dummy call just to determine the actual size required */
        SetupDiGetDeviceInterfaceDetail(deviceInfoList, &deviceInfo, NULL, 0, &size, NULL);
        if(deviceDetails != NULL)
            free(deviceDetails);
        deviceDetails = malloc(size);
        deviceDetails->cbSize = sizeof(*deviceDetails);
        /* this call is for real: */
        SetupDiGetDeviceInterfaceDetail(deviceInfoList, &deviceInfo, deviceDetails,
					size, &size, NULL);
        DEBUG_PRINT(("checking HID path \"%s\"\n", deviceDetails->DevicePath));
        /* attempt opening for R/W -- we don't care about devices which can't be accessed */
        handle = CreateFile(deviceDetails->DevicePath, GENERIC_READ|GENERIC_WRITE,
			    FILE_SHARE_READ|FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			    openFlag, NULL);
        if(handle == INVALID_HANDLE_VALUE){
            DEBUG_PRINT(("opening failed: %d\n", (int)GetLastError()));
            /* errorCode = USB_ERROR_ACCESS; opening will always fail for mouse -- ignore */
            continue;
        }
        deviceAttributes.Size = sizeof(deviceAttributes);
        HidD_GetAttributes(handle, &deviceAttributes);
        DEBUG_PRINT(("device attributes: vid=%d pid=%d\n",
		     deviceAttributes.VendorID, deviceAttributes.ProductID));
        if(deviceAttributes.VendorID != vendor || deviceAttributes.ProductID != product)
            continue;   /* ignore this device */
        errorCode = USB_ERROR_NOTFOUND;
        if(vendorName != NULL && productName != NULL){
            char    buffer[512];
            if(!HidD_GetManufacturerString(handle, buffer, sizeof(buffer))){
                DEBUG_PRINT(("error obtaining vendor name\n"));
                errorCode = USB_ERROR_IO;
                continue;
            }
            convertUniToAscii(buffer);
            DEBUG_PRINT(("vendorName = \"%s\"\n", buffer));
            if(strcmp(vendorName, buffer) != 0)
                continue;
            if(!HidD_GetProductString(handle, buffer, sizeof(buffer))){
                DEBUG_PRINT(("error obtaining product name\n"));
                errorCode = USB_ERROR_IO;
                continue;
            }
            convertUniToAscii(buffer);
            DEBUG_PRINT(("productName = \"%s\"\n", buffer));
            if(strcmp(productName, buffer) != 0)
                continue;
        }
        break;  /* we have found the device we are looking for! */
    }
    SetupDiDestroyDeviceInfoList(deviceInfoList);
    if(deviceDetails != NULL)
        free(deviceDetails);
    if(handle != INVALID_HANDLE_VALUE){
	fdp->pfd = (void *)handle;
	errorCode = 0;
    }
    return errorCode;
}

/* ------------------------------------------------------------------------ */

static void    usbCloseDevice(union filedescriptor *fdp)
{
    CloseHandle((HANDLE)fdp->pfd);
}

/* ------------------------------------------------------------------------ */

static int usbSetReport(union filedescriptor *fdp, int reportType, char *buffer, int len)
{
    HANDLE  handle = (HANDLE)fdp->pfd;
    BOOLEAN rval = 0;
    DWORD   bytesWritten;

    switch(reportType){
    case USB_HID_REPORT_TYPE_INPUT:
        break;
    case USB_HID_REPORT_TYPE_OUTPUT:
        rval = WriteFile(handle, buffer, len, &bytesWritten, NULL);
        break;
    case USB_HID_REPORT_TYPE_FEATURE:
        rval = HidD_SetFeature(handle, buffer, len);
        break;
    }
    return rval == 0 ? USB_ERROR_IO : 0;
}

/* ------------------------------------------------------------------------ */

static int usbGetReport(union filedescriptor *fdp, int reportType, int reportNumber,
			char *buffer, int *len)
{
    HANDLE  handle = (HANDLE)fdp->pfd;
    BOOLEAN rval = 0;
    DWORD   bytesRead;

    switch(reportType){
    case USB_HID_REPORT_TYPE_INPUT:
        buffer[0] = reportNumber;
        rval = ReadFile(handle, buffer, *len, &bytesRead, NULL);
        if(rval)
            *len = bytesRead;
        break;
    case USB_HID_REPORT_TYPE_OUTPUT:
        break;
    case USB_HID_REPORT_TYPE_FEATURE:
        buffer[0] = reportNumber;
        rval = HidD_GetFeature(handle, buffer, *len);
        break;
    }
    return rval == 0 ? USB_ERROR_IO : 0;
}

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#else /* !(WIN32NATIVE && HAVE_LIBHID) */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

#if defined(HAVE_USB_H)
#  include <usb.h>
#elif defined(HAVE_LUSB0_USB_H)
#  include <lusb0_usb.h>
#else
#  error "libusb needs either <usb.h> or <lusb0_usb.h>"
#endif

/* ------------------------------------------------------------------------- */

#define USBRQ_HID_GET_REPORT    0x01
#define USBRQ_HID_SET_REPORT    0x09

static int  usesReportIDs;

/* ------------------------------------------------------------------------- */

static int  usbGetStringAscii(usb_dev_handle *dev, int index, int langid, char *buf, int buflen)
{
    char    buffer[256];
    int     rval, i;

    if((rval = usb_control_msg(dev, USB_ENDPOINT_IN, USB_REQ_GET_DESCRIPTOR,
			       (USB_DT_STRING << 8) + index, langid, buffer,
			       sizeof(buffer), 1000)) < 0)
        return rval;
    if(buffer[1] != USB_DT_STRING)
        return 0;
    if((unsigned char)buffer[0] < rval)
        rval = (unsigned char)buffer[0];
    rval /= 2;
    /* lossy conversion to ISO Latin1 */
    for(i=1;i<rval;i++){
        if(i > buflen)  /* destination buffer overflow */
            break;
        buf[i-1] = buffer[2 * i];
        if(buffer[2 * i + 1] != 0)  /* outside of ISO Latin1 range */
            buf[i-1] = '?';
    }
    buf[i-1] = 0;
    return i-1;
}

static int usbOpenDevice(union filedescriptor *fdp, int vendor, char *vendorName,
			 int product, char *productName, int doReportIDs)
{
    struct usb_bus      *bus;
    struct usb_device   *dev;
    usb_dev_handle      *handle = NULL;
    int                 errorCode = USB_ERROR_NOTFOUND;
    static int          didUsbInit = 0;

    if(!didUsbInit){
        usb_init();
        didUsbInit = 1;
    }
    usb_find_busses();
    usb_find_devices();
    for(bus=usb_get_busses(); bus; bus=bus->next){
        for(dev=bus->devices; dev; dev=dev->next){
            if(dev->descriptor.idVendor == vendor && dev->descriptor.idProduct == product){
                char    string[256];
                int     len;
                handle = usb_open(dev); /* we need to open the device in order to query strings */
                if(!handle){
                    errorCode = USB_ERROR_ACCESS;
                    avrdude_message(MSG_INFO, "Warning: cannot open USB device: %s\n",
			    usb_strerror());
                    continue;
                }
                if(vendorName == NULL && productName == NULL){  /* name does not matter */
                    break;
                }
                /* now check whether the names match: */
                len = usbGetStringAscii(handle, dev->descriptor.iManufacturer,
					0x0409, string, sizeof(string));
                if(len < 0){
                    errorCode = USB_ERROR_IO;
                    avrdude_message(MSG_INFO, "Warning: cannot query manufacturer for device: %s\n",
                                    usb_strerror());
                }else{
                    errorCode = USB_ERROR_NOTFOUND;
                    /* avrdude_message(MSG_INFO, "seen device from vendor ->%s<-\n", string); */
                    if(strcmp(string, vendorName) == 0){
                        len = usbGetStringAscii(handle, dev->descriptor.iProduct,
						0x0409, string, sizeof(string));
                        if(len < 0){
                            errorCode = USB_ERROR_IO;
                            avrdude_message(MSG_INFO, "Warning: cannot query product for device: %s\n",
                                            usb_strerror());
                        }else{
                            errorCode = USB_ERROR_NOTFOUND;
                            /* avrdude_message(MSG_INFO, "seen product ->%s<-\n", string); */
                            if(strcmp(string, productName) == 0)
                                break;
                        }
                    }
                }
                usb_close(handle);
                handle = NULL;
            }
        }
        if(handle)
            break;
    }
    if(handle != NULL){
        int rval, retries = 3;
        if(usb_set_configuration(handle, 1)){
            avrdude_message(MSG_INFO, "Warning: could not set configuration: %s\n",
		    usb_strerror());
        }
        /* now try to claim the interface and detach the kernel HID driver on
         * linux and other operating systems which support the call.
         */
        while((rval = usb_claim_interface(handle, 0)) != 0 && retries-- > 0){
#ifdef LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP
            if(usb_detach_kernel_driver_np(handle, 0) < 0){
                avrdude_message(MSG_INFO, "Warning: could not detach kernel HID driver: %s\n",
			usb_strerror());
            }
#endif
        }
        if(rval != 0)
            avrdude_message(MSG_INFO, "Warning: could not claim interface\n");
/* Continue anyway, even if we could not claim the interface. Control transfers
 * should still work.
 */
        errorCode = 0;
        fdp->pfd = (void *)handle;
        usesReportIDs = doReportIDs;
    }
    return errorCode;
}

/* ------------------------------------------------------------------------- */

static void    usbCloseDevice(union filedescriptor *fdp)
{
    usb_close((usb_dev_handle *)fdp->pfd);
}

/* ------------------------------------------------------------------------- */

static int usbSetReport(union filedescriptor *fdp, int reportType, char *buffer, int len)
{
    int bytesSent;

    if(!usesReportIDs){
        buffer++;   /* skip dummy report ID */
        len--;
    }
    bytesSent = usb_control_msg((usb_dev_handle *)fdp->pfd, USB_TYPE_CLASS |
				USB_RECIP_INTERFACE | USB_ENDPOINT_OUT, USBRQ_HID_SET_REPORT,
				reportType << 8 | buffer[0], 0, buffer, len, 5000);
    if(bytesSent != len){
        if(bytesSent < 0)
            avrdude_message(MSG_INFO, "Error sending message: %s\n", usb_strerror());
        return USB_ERROR_IO;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

static int usbGetReport(union filedescriptor *fdp, int reportType, int reportNumber,
			char *buffer, int *len)
{
    int bytesReceived, maxLen = *len;

    if(!usesReportIDs){
        buffer++;   /* make room for dummy report ID */
        maxLen--;
    }
    bytesReceived = usb_control_msg((usb_dev_handle *)fdp->pfd, USB_TYPE_CLASS |
				    USB_RECIP_INTERFACE | USB_ENDPOINT_IN, USBRQ_HID_GET_REPORT,
				    reportType << 8 | reportNumber, 0, buffer, maxLen, 5000);
    if(bytesReceived < 0){
        avrdude_message(MSG_INFO, "Error sending message: %s\n", usb_strerror());
        return USB_ERROR_IO;
    }
    *len = bytesReceived;
    if(!usesReportIDs){
        buffer[-1] = reportNumber;  /* add dummy report ID */
        len++;
    }
    return 0;
}

#endif  /* WIN32NATIVE */

/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */
/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------- */

static void dumpBlock(const char *prefix, const unsigned char *buf, int len)
{
    int i;

    if(len <= 8){   /* more compact format for short blocks */
        avrdude_message(MSG_INFO, "%s: %d bytes: ", prefix, len);
        for(i = 0; i < len; i++){
            avrdude_message(MSG_INFO, "%02x ", buf[i]);
        }
        avrdude_message(MSG_INFO, " \"");
        for(i = 0; i < len; i++){
            if(buf[i] >= 0x20 && buf[i] < 0x7f){
                fputc(buf[i], stderr);
            }else{
                fputc('.', stderr);
            }
        }
        avrdude_message(MSG_INFO, "\"\n");
    }else{
        avrdude_message(MSG_INFO, "%s: %d bytes:\n", prefix, len);
        while(len > 0){
            for(i = 0; i < 16; i++){
                if(i < len){
                    avrdude_message(MSG_INFO, "%02x ", buf[i]);
                }else{
                    avrdude_message(MSG_INFO, "   ");
                }
                if(i == 7)
                    fputc(' ', stderr);
            }
            avrdude_message(MSG_INFO, "  \"");
            for(i = 0; i < 16; i++){
                if(i < len){
                    if(buf[i] >= 0x20 && buf[i] < 0x7f){
                        fputc(buf[i], stderr);
                    }else{
                        fputc('.', stderr);
                    }
                }
            }
            avrdude_message(MSG_INFO, "\"\n");
            buf += 16;
            len -= 16;
        }
    }
}

static char *usbErrorText(int usbErrno)
{
    static char buffer[32];

    switch(usbErrno){
        case USB_ERROR_NONE:    return "Success.";
        case USB_ERROR_ACCESS:  return "Access denied.";
        case USB_ERROR_NOTFOUND:return "Device not found.";
        case USB_ERROR_BUSY:    return "Device is busy.";
        case USB_ERROR_IO:      return "I/O Error.";
        default:
            sprintf(buffer, "Unknown error %d.", usbErrno);
            return buffer;
    }
}

/* ------------------------------------------------------------------------- */

static int avrdoper_open(char *port, union pinfo pinfo, union filedescriptor *fdp)
{
    int rval;
    char *vname = "obdev.at";
    char *devname = "AVR-Doper";

    rval = usbOpenDevice(fdp, USB_VENDOR_ID, vname, USB_PRODUCT_ID, devname, 1);
    if(rval != 0){
        avrdude_message(MSG_INFO, "%s: avrdoper_open(): %s\n", progname, usbErrorText(rval));
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

static void avrdoper_close(union filedescriptor *fdp)
{
    usbCloseDevice(fdp);
}

/* ------------------------------------------------------------------------- */

static int  chooseDataSize(int len)
{
    int i;

    for(i = 0; i < sizeof(reportDataSizes)/sizeof(reportDataSizes[0]); i++){
        if(reportDataSizes[i] >= len)
            return i;
    }
    return i - 1;
}

static int avrdoper_send(union filedescriptor *fdp, const unsigned char *buf, size_t buflen)
{
    if(verbose > 3)
        dumpBlock("Send", buf, buflen);
    while(buflen > 0){
        unsigned char buffer[256];
        int rval, lenIndex = chooseDataSize(buflen);
        int thisLen = buflen > reportDataSizes[lenIndex] ?
	    reportDataSizes[lenIndex] : buflen;
        buffer[0] = lenIndex + 1;   /* report ID */
        buffer[1] = thisLen;
        memcpy(buffer + 2, buf, thisLen);
        avrdude_message(MSG_TRACE, "Sending %d bytes data chunk\n", thisLen);
        rval = usbSetReport(fdp, USB_HID_REPORT_TYPE_FEATURE, (char *)buffer,
			    reportDataSizes[lenIndex] + 2);
        if(rval != 0){
            avrdude_message(MSG_INFO, "%s: avrdoper_send(): %s\n", progname, usbErrorText(rval));
            return -1;
        }
        buflen -= thisLen;
        buf += thisLen;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */

static int avrdoperFillBuffer(union filedescriptor *fdp)
{
    int bytesPending = reportDataSizes[1];  /* guess how much data is buffered in device */

    avrdoperRxPosition = avrdoperRxLength = 0;
    while(bytesPending > 0){
        int len, usbErr, lenIndex = chooseDataSize(bytesPending);
        unsigned char buffer[128];
        len = sizeof(avrdoperRxBuffer) - avrdoperRxLength;  /* bytes remaining */
        if(reportDataSizes[lenIndex] + 2 > len) /* requested data would not fit into buffer */
            break;
        len = reportDataSizes[lenIndex] + 2;
        usbErr = usbGetReport(fdp, USB_HID_REPORT_TYPE_FEATURE, lenIndex + 1,
			      (char *)buffer, &len);
        if(usbErr != 0){
            avrdude_message(MSG_INFO, "%s: avrdoperFillBuffer(): %s\n", progname, usbErrorText(usbErr));
            return -1;
        }
        avrdude_message(MSG_TRACE, "Received %d bytes data chunk of total %d\n", len - 2, buffer[1]);
        len -= 2;   /* compensate for report ID and length byte */
        bytesPending = buffer[1] - len; /* amount still buffered */
        if(len > buffer[1])             /* cut away padding */
            len = buffer[1];
        if(avrdoperRxLength + len > sizeof(avrdoperRxBuffer)){
            avrdude_message(MSG_INFO, "%s: avrdoperFillBuffer(): internal error: buffer overflow\n",
                            progname);
            return -1;
        }
        memcpy(avrdoperRxBuffer + avrdoperRxLength, buffer + 2, len);
        avrdoperRxLength += len;
    }
    return 0;
}

static int avrdoper_recv(union filedescriptor *fdp, unsigned char *buf, size_t buflen)
{
    unsigned char   *p = buf;
    int             remaining = buflen;

    while(remaining > 0){
        int len, available = avrdoperRxLength - avrdoperRxPosition;
        if(available <= 0){ /* buffer is empty */
            if (avrdoperFillBuffer(fdp) < 0)
                return -1;
            continue;
        }
        len = remaining < available ? remaining : available;
        memcpy(p, avrdoperRxBuffer + avrdoperRxPosition, len);
        p += len;
        remaining -= len;
        avrdoperRxPosition += len;
    }
    if(verbose > 3)
        dumpBlock("Receive", buf, buflen);
    return 0;
}

/* ------------------------------------------------------------------------- */

static int avrdoper_drain(union filedescriptor *fdp, int display)
{
    do{
        if (avrdoperFillBuffer(fdp) < 0)
            return -1;
    }while(avrdoperRxLength > 0);
    return 0;
}

/* ------------------------------------------------------------------------- */

static int avrdoper_set_dtr_rts(union filedescriptor *fdp, int is_on)
{
	avrdude_message(MSG_INFO, "%s: AVR-Doper doesn't support DTR/RTS setting\n", progname);
    return -1;
}

/* ------------------------------------------------------------------------- */

struct serial_device avrdoper_serdev =
{
  .open = avrdoper_open,
  .close = avrdoper_close,
  .send = avrdoper_send,
  .recv = avrdoper_recv,
  .drain = avrdoper_drain,
  .set_dtr_rts = avrdoper_set_dtr_rts,
  .flags = SERDEV_FL_NONE,
};

#endif /* defined(HAVE_LIBUSB) || (defined(WIN32NATIVE) && defined(HAVE_LIBHID)) */
