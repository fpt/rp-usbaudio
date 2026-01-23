/*
             LUFA Library
     Copyright (C) Dean Camera, 2020.
  dean [at] fourwalledcubicle [dot] com
           www.lufa-lib.org
*/
/*
  Copyright 2020  Dean Camera (dean [at] fourwalledcubicle [dot] com)
  Permission to use, copy, modify, distribute, and sell this software and its
  documentation for any purpose is hereby granted without fee, provided that
  the above copyright notice appear in all copies and that both that the
  copyright notice and this permission notice and warranty disclaimer appear in
  supporting documentation, and that the name of the author not be used in
  advertising or publicity pertaining to distribution of the software without
  specific, written permission. The author disclaims all warranties with regard
  to this software, including all implied warranties of merchantability and
  fitness. In no event shall the author be liable for any special, indirect or
  consequential damages or any damages whatsoever resulting from loss of use,
  data or profits, whether in an action of contract, negligence or other
  tortious action, arising out of or in connection with the use or performance
  of this software.
*/

#ifndef __USBDESCRIPTORS_H__
#define __USBDESCRIPTORS_H__

#define ATTR_PACKED __packed
#define CPU_TO_LE16(x) (x)

#if defined(__cplusplus)
extern "C" {
#endif

#define NO_DESCRIPTOR                     0
#define USB_CONFIG_POWER_MA(mA)           ((mA) >> 1)
#define USB_STRING_LEN(UnicodeChars)      (sizeof(USB_Descriptor_Header_t) + ((UnicodeChars) << 1))

#define VERSION_BCD(Major, Minor, Revision) \
    CPU_TO_LE16( ((Major & 0xFF) << 8) | \
                 ((Minor & 0x0F) << 4) | \
                 (Revision & 0x0F) )

#define LANGUAGE_ID_ENG                   0x0409
#define USB_CONFIG_ATTR_RESERVED          0x80
#define USB_CONFIG_ATTR_SELFPOWERED       0x40
#define USB_CONFIG_ATTR_REMOTEWAKEUP      0x20

#define ENDPOINT_ATTR_NO_SYNC             (0 << 2)
#define ENDPOINT_ATTR_ASYNC               (1 << 2)
#define ENDPOINT_ATTR_ADAPTIVE            (2 << 2)
#define ENDPOINT_ATTR_SYNC                (3 << 2)
#define ENDPOINT_USAGE_DATA               (0 << 4)
#define ENDPOINT_USAGE_FEEDBACK           (1 << 4)
#define ENDPOINT_USAGE_IMPLICIT_FEEDBACK  (2 << 4)

enum USB_DescriptorTypes_t {
    DTYPE_Device               = 0x01,
    DTYPE_Configuration        = 0x02,
    DTYPE_String               = 0x03,
    DTYPE_Interface            = 0x04,
    DTYPE_Endpoint             = 0x05,
    DTYPE_DeviceQualifier      = 0x06,
    DTYPE_Other                = 0x07,
    DTYPE_InterfacePower       = 0x08,
    DTYPE_InterfaceAssociation = 0x0B,
};

typedef struct {
    uint8_t Size;
    uint8_t Type;
} ATTR_PACKED USB_Descriptor_Header_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
} ATTR_PACKED USB_StdDescriptor_Header_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} ATTR_PACKED USB_StdDescriptor_Device_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} ATTR_PACKED USB_StdDescriptor_Configuration_Header_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} ATTR_PACKED USB_StdDescriptor_Interface_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} ATTR_PACKED USB_StdDescriptor_Endpoint_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
    uint8_t  bRefresh;
    uint8_t  bSynchAddress;
} ATTR_PACKED USB_Audio_StdDescriptor_StreamEndpoint_Std_t;

typedef struct {
    USB_Descriptor_Header_t Header;
    uint8_t  EndpointAddress;
    uint8_t  Attributes;
    uint16_t EndpointSize;
    uint8_t  PollingIntervalMS;
} ATTR_PACKED USB_Descriptor_Endpoint_t;

#if defined(__cplusplus)
}
#endif

#endif
