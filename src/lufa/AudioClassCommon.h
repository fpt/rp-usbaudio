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

#ifndef _AUDIO_CLASS_COMMON_H_
#define _AUDIO_CLASS_COMMON_H_

#include "StdDescriptors.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define AUDIO_CHANNEL_LEFT_FRONT           (1 << 0)
#define AUDIO_CHANNEL_RIGHT_FRONT          (1 << 1)
#define AUDIO_CHANNEL_CENTER_FRONT         (1 << 2)
#define AUDIO_CHANNEL_LOW_FREQ_ENHANCE     (1 << 3)
#define AUDIO_CHANNEL_LEFT_SURROUND        (1 << 4)
#define AUDIO_CHANNEL_RIGHT_SURROUND       (1 << 5)

#define AUDIO_FEATURE_MUTE                 (1 << 0)
#define AUDIO_FEATURE_VOLUME               (1 << 1)
#define AUDIO_FEATURE_BASS                 (1 << 2)
#define AUDIO_FEATURE_MID                  (1 << 3)
#define AUDIO_FEATURE_TREBLE               (1 << 4)

#define AUDIO_TERMINAL_UNDEFINED           0x0100
#define AUDIO_TERMINAL_STREAMING           0x0101
#define AUDIO_TERMINAL_VENDOR              0x01FF
#define AUDIO_TERMINAL_IN_UNDEFINED        0x0200
#define AUDIO_TERMINAL_IN_MIC              0x0201
#define AUDIO_TERMINAL_OUT_SPEAKER         0x0301
#define AUDIO_TERMINAL_OUT_HEADPHONES      0x0302
#define AUDIO_TERMINAL_OUT_HEAD_MOUNTED    0x0303
#define AUDIO_TERMINAL_OUT_DESKTOP         0x0304

#define AUDIO_SAMPLE_FREQ(freq) \
    {.Byte1 = ((uint32_t)(freq) & 0xFF), \
     .Byte2 = (((uint32_t)(freq) >> 8) & 0xFF), \
     .Byte3 = (((uint32_t)(freq) >> 16) & 0xFF)}

#define AUDIO_EP_FULL_PACKETS_ONLY        (1 << 7)
#define AUDIO_EP_ACCEPTS_SMALL_PACKETS    (0 << 7)
#define AUDIO_EP_SAMPLE_FREQ_CONTROL      (1 << 0)
#define AUDIO_EP_PITCH_CONTROL            (1 << 1)

enum Audio_Descriptor_ClassSubclassProtocol_t {
    AUDIO_CSCP_AudioClass             = 0x01,
    AUDIO_CSCP_ControlSubclass        = 0x01,
    AUDIO_CSCP_ControlProtocol        = 0x00,
    AUDIO_CSCP_AudioStreamingSubclass = 0x02,
    AUDIO_CSCP_MIDIStreamingSubclass  = 0x03,
    AUDIO_CSCP_StreamingProtocol      = 0x00,
};

enum AUDIO_DescriptorTypes_t {
    AUDIO_DTYPE_CSInterface = 0x24,
    AUDIO_DTYPE_CSEndpoint  = 0x25,
};

enum Audio_CSInterface_AC_SubTypes_t {
    AUDIO_DSUBTYPE_CSInterface_Header         = 0x01,
    AUDIO_DSUBTYPE_CSInterface_InputTerminal  = 0x02,
    AUDIO_DSUBTYPE_CSInterface_OutputTerminal = 0x03,
    AUDIO_DSUBTYPE_CSInterface_Mixer          = 0x04,
    AUDIO_DSUBTYPE_CSInterface_Selector       = 0x05,
    AUDIO_DSUBTYPE_CSInterface_Feature        = 0x06,
    AUDIO_DSUBTYPE_CSInterface_Processing     = 0x07,
    AUDIO_DSUBTYPE_CSInterface_Extension      = 0x08,
};

enum Audio_CSInterface_AS_SubTypes_t {
    AUDIO_DSUBTYPE_CSInterface_General        = 0x01,
    AUDIO_DSUBTYPE_CSInterface_FormatType     = 0x02,
    AUDIO_DSUBTYPE_CSInterface_FormatSpecific = 0x03,
};

enum Audio_CSEndpoint_SubTypes_t {
    AUDIO_DSUBTYPE_CSEndpoint_General = 0x01,
};

enum Audio_ClassRequests_t {
    AUDIO_REQ_SetCurrent    = 0x01,
    AUDIO_REQ_SetMinimum    = 0x02,
    AUDIO_REQ_SetMaximum    = 0x03,
    AUDIO_REQ_SetResolution = 0x04,
    AUDIO_REQ_SetMemory     = 0x05,
    AUDIO_REQ_GetCurrent    = 0x81,
    AUDIO_REQ_GetMinimum    = 0x82,
    AUDIO_REQ_GetMaximum    = 0x83,
    AUDIO_REQ_GetResolution = 0x84,
    AUDIO_REQ_GetMemory     = 0x85,
    AUDIO_REQ_GetStatus     = 0xFF,
};

enum Audio_EndpointControls_t {
    AUDIO_EPCONTROL_SamplingFreq = 0x01,
    AUDIO_EPCONTROL_Pitch        = 0x02,
};

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  bNrChannels;
    uint16_t wChannelConfig;
    uint8_t  iChannelNames;
    uint8_t  iTerminal;
} ATTR_PACKED USB_Audio_StdDescriptor_InputTerminal_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bTerminalID;
    uint16_t wTerminalType;
    uint8_t  bAssocTerminal;
    uint8_t  bSourceID;
    uint8_t  iTerminal;
} ATTR_PACKED USB_Audio_StdDescriptor_OutputTerminal_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint16_t bcdADC;
    uint16_t wTotalLength;
    uint8_t  bInCollection;
    uint8_t  bInterfaceNumbers;
} ATTR_PACKED USB_Audio_StdDescriptor_Interface_AC_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bUnitID;
    uint8_t bSourceID;
    uint8_t bControlSize;
    uint8_t bmaControls[3];
    uint8_t iFeature;
} ATTR_PACKED USB_Audio_StdDescriptor_FeatureUnit_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bTerminalLink;
    uint8_t  bDelay;
    uint16_t wFormatTag;
} ATTR_PACKED USB_Audio_StdDescriptor_Interface_AS_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bDescriptorSubtype;
    uint8_t bFormatType;
    uint8_t bNrChannels;
    uint8_t bSubFrameSize;
    uint8_t bBitResolution;
    uint8_t bSampleFrequencyType;
} ATTR_PACKED USB_Audio_StdDescriptor_Format_t;

typedef struct {
    uint8_t Byte1;
    uint8_t Byte2;
    uint8_t Byte3;
} ATTR_PACKED USB_Audio_SampleFreq_t;

typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bDescriptorSubtype;
    uint8_t  bmAttributes;
    uint8_t  bLockDelayUnits;
    uint16_t wLockDelay;
} ATTR_PACKED USB_Audio_StdDescriptor_StreamEndpoint_Spc_t;

#if defined(__cplusplus)
}
#endif

#endif
