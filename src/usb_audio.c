// SPDX-License-Identifier: MIT (modifications) / BSD-3-Clause (original pico-playground)
// USB Audio Class 1.0 (UAC1) implementation
//
// Adapted from raspberrypi/pico-playground/apps/usb_sound_card/usb_sound_card.c
// Key changes from original:
//   - Audio output selectable at build time: I2S (default) or PDM (-DAUDIO_PDM)
//   - Added proportional feedback based on audio ring buffer fill level
//   - Added ASRC for secondary clock drift compensation
//   - Removed TinyUSB; uses pico-extras USB device stack

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/usb_device.h"
#include "hardware/clocks.h"
#include "lufa/AudioClassCommon.h"

#include "asrc.h"
#include "audio_out.h"
#include "usb_audio.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

#define VENDOR_ID   0x2e8au
#define PRODUCT_ID  0xfeddu

#define AUDIO_OUT_ENDPOINT  0x01u
#define AUDIO_IN_ENDPOINT   0x82u

// Override LUFA's AUDIO_SAMPLE_FREQ macro with the raw-bytes version used in descriptor
#undef AUDIO_SAMPLE_FREQ
#define AUDIO_SAMPLE_FREQ(frq) \
    (uint8_t)(frq), (uint8_t)((frq) >> 8), (uint8_t)((frq) >> 16)

// Max packet size: (max_freq / 1000) * 4 bytes/frame + 4 byte headroom
#define AUDIO_MAX_PACKET_SIZE(freq) (uint8_t)(((freq + 999) / 1000) * 4)

#define FEATURE_MUTE_CONTROL    1u
#define FEATURE_VOLUME_CONTROL  2u
#define ENDPOINT_FREQ_CONTROL   1u

//--------------------------------------------------------------------
// Descriptor strings
//--------------------------------------------------------------------

static const char *descriptor_strings[] = {
    "Raspberry Pi",           // 1: Manufacturer
    "RP2350 USB Audio DAC",   // 2: Product
    "0123456789AB",           // 3: Serial
};

//--------------------------------------------------------------------
// Descriptor structure
//--------------------------------------------------------------------

struct audio_device_config {
    struct usb_configuration_descriptor descriptor;
    struct usb_interface_descriptor ac_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AC_t  core;
        USB_Audio_StdDescriptor_InputTerminal_t input_terminal;
        USB_Audio_StdDescriptor_FeatureUnit_t   feature_unit;
        USB_Audio_StdDescriptor_OutputTerminal_t output_terminal;
    } ac_audio;
    struct usb_interface_descriptor as_zero_interface;
    struct usb_interface_descriptor as_op_interface;
    struct __packed {
        USB_Audio_StdDescriptor_Interface_AS_t streaming;
        struct __packed {
            USB_Audio_StdDescriptor_Format_t core;
            USB_Audio_SampleFreq_t freqs[2];
        } format;
    } as_audio;
    struct __packed {
        struct usb_endpoint_descriptor_long core;
        USB_Audio_StdDescriptor_StreamEndpoint_Spc_t audio;
    } ep1;
    struct usb_endpoint_descriptor_long ep2;
};

static const struct audio_device_config audio_device_config = {
    .descriptor = {
        .bLength             = sizeof(audio_device_config.descriptor),
        .bDescriptorType     = DTYPE_Configuration,
        .wTotalLength        = sizeof(audio_device_config),
        .bNumInterfaces      = 2,
        .bConfigurationValue = 0x01,
        .iConfiguration      = 0x00,
        .bmAttributes        = 0x80,
        .bMaxPower           = 0x32,
    },
    .ac_interface = {
        .bLength            = sizeof(audio_device_config.ac_interface),
        .bDescriptorType    = DTYPE_Interface,
        .bInterfaceNumber   = 0x00,
        .bAlternateSetting  = 0x00,
        .bNumEndpoints      = 0x00,
        .bInterfaceClass    = AUDIO_CSCP_AudioClass,
        .bInterfaceSubClass = AUDIO_CSCP_ControlSubclass,
        .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
        .iInterface         = 0x00,
    },
    .ac_audio = {
        .core = {
            .bLength           = sizeof(audio_device_config.ac_audio.core),
            .bDescriptorType   = AUDIO_DTYPE_CSInterface,
            .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Header,
            .bcdADC            = VERSION_BCD(1, 0, 0),
            .wTotalLength      = sizeof(audio_device_config.ac_audio),
            .bInCollection     = 1,
            .bInterfaceNumbers = 1,
        },
        .input_terminal = {
            .bLength           = sizeof(audio_device_config.ac_audio.input_terminal),
            .bDescriptorType   = AUDIO_DTYPE_CSInterface,
            .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_InputTerminal,
            .bTerminalID       = 1,
            .wTerminalType     = AUDIO_TERMINAL_STREAMING,
            .bAssocTerminal    = 0,
            .bNrChannels       = 2,
            .wChannelConfig    = AUDIO_CHANNEL_LEFT_FRONT | AUDIO_CHANNEL_RIGHT_FRONT,
            .iChannelNames     = 0,
            .iTerminal         = 0,
        },
        .feature_unit = {
            .bLength           = sizeof(audio_device_config.ac_audio.feature_unit),
            .bDescriptorType   = AUDIO_DTYPE_CSInterface,
            .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_Feature,
            .bUnitID           = 2,
            .bSourceID         = 1,
            .bControlSize      = 1,
            .bmaControls       = {AUDIO_FEATURE_MUTE | AUDIO_FEATURE_VOLUME, 0, 0},
            .iFeature          = 0,
        },
        .output_terminal = {
            .bLength           = sizeof(audio_device_config.ac_audio.output_terminal),
            .bDescriptorType   = AUDIO_DTYPE_CSInterface,
            .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_OutputTerminal,
            .bTerminalID       = 3,
            .wTerminalType     = AUDIO_TERMINAL_OUT_SPEAKER,
            .bAssocTerminal    = 0,
            .bSourceID         = 2,
            .iTerminal         = 0,
        },
    },
    .as_zero_interface = {
        .bLength            = sizeof(audio_device_config.as_zero_interface),
        .bDescriptorType    = DTYPE_Interface,
        .bInterfaceNumber   = 0x01,
        .bAlternateSetting  = 0x00,
        .bNumEndpoints      = 0x00,
        .bInterfaceClass    = AUDIO_CSCP_AudioClass,
        .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
        .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
        .iInterface         = 0x00,
    },
    .as_op_interface = {
        .bLength            = sizeof(audio_device_config.as_op_interface),
        .bDescriptorType    = DTYPE_Interface,
        .bInterfaceNumber   = 0x01,
        .bAlternateSetting  = 0x01,
        .bNumEndpoints      = 0x02,
        .bInterfaceClass    = AUDIO_CSCP_AudioClass,
        .bInterfaceSubClass = AUDIO_CSCP_AudioStreamingSubclass,
        .bInterfaceProtocol = AUDIO_CSCP_ControlProtocol,
        .iInterface         = 0x00,
    },
    .as_audio = {
        .streaming = {
            .bLength           = sizeof(audio_device_config.as_audio.streaming),
            .bDescriptorType   = AUDIO_DTYPE_CSInterface,
            .bDescriptorSubtype = AUDIO_DSUBTYPE_CSInterface_General,
            .bTerminalLink     = 1,
            .bDelay            = 1,
            .wFormatTag        = 1, // PCM
        },
        .format = {
            .core = {
                .bLength              = sizeof(audio_device_config.as_audio.format),
                .bDescriptorType      = AUDIO_DTYPE_CSInterface,
                .bDescriptorSubtype   = AUDIO_DSUBTYPE_CSInterface_FormatType,
                .bFormatType          = 1,
                .bNrChannels          = 2,
                .bSubFrameSize        = 2,
                .bBitResolution       = 16,
                .bSampleFrequencyType = 2,
            },
            .freqs = {
                AUDIO_SAMPLE_FREQ(44100),
                AUDIO_SAMPLE_FREQ(48000),
            },
        },
    },
    .ep1 = {
        .core = {
            .bLength          = sizeof(audio_device_config.ep1.core),
            .bDescriptorType  = DTYPE_Endpoint,
            .bEndpointAddress = AUDIO_OUT_ENDPOINT,
            .bmAttributes     = 5, // isochronous asynchronous
            .wMaxPacketSize   = AUDIO_MAX_PACKET_SIZE(48000),
            .bInterval        = 1,
            .bRefresh         = 0,
            .bSyncAddr        = AUDIO_IN_ENDPOINT,
        },
        .audio = {
            .bLength              = sizeof(audio_device_config.ep1.audio),
            .bDescriptorType      = AUDIO_DTYPE_CSEndpoint,
            .bDescriptorSubtype   = AUDIO_DSUBTYPE_CSEndpoint_General,
            .bmAttributes         = 1, // sampling freq control
            .bLockDelayUnits      = 0,
            .wLockDelay           = 0,
        },
    },
    .ep2 = {
        .bLength          = sizeof(audio_device_config.ep2),
        .bDescriptorType  = DTYPE_Endpoint,
        .bEndpointAddress = AUDIO_IN_ENDPOINT,
        .bmAttributes     = 0x11, // isochronous feedback
        .wMaxPacketSize   = 3,
        .bInterval        = 1,
        .bRefresh         = 2,    // every 2^2 = 4 ms
        .bSyncAddr        = 0,
    },
};

static const struct usb_device_descriptor boot_device_descriptor = {
    .bLength            = 18,
    .bDescriptorType    = 0x01,
    .bcdUSB             = 0x0110,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 0x40,
    .idVendor           = VENDOR_ID,
    .idProduct          = PRODUCT_ID,
    .bcdDevice          = 0x0200,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static struct usb_interface ac_interface;
static struct usb_interface as_op_interface;
static struct usb_endpoint ep_op_out, ep_op_sync;

static struct {
    uint32_t freq;
    bool mute;
    int16_t volume;      // encoded as dB * 256
    uint8_t volume_pct;  // 0..100 for audio_out
} _state = {
    .freq       = 48000,
    .mute       = false,
    .volume     = 0,     // 0 dB
    .volume_pct = 100,
};

static bool _streaming = false;

// Feedback value for EP2, read by sync handler.
// Initialised to nominal 48 kHz; the host uses this to pace its sends.
// ASRC provides the actual clock drift correction on the device side.
static volatile uint32_t _feedback_value = (48000u << 14u) / 1000u;

// Peak level tracking for VU meter display
static volatile int32_t _peak_left  = 0;
static volatile int32_t _peak_right = 0;

//--------------------------------------------------------------------
// Volume dB → percent conversion
// UAC1 encodes volume as signed 8.8 fixed-point (dB).
// We support −90 dB..0 dB → 0%..100%.
//--------------------------------------------------------------------

#define VOLUME_DB_MIN  (-90 * 256)   // -90 dB
#define VOLUME_DB_MAX  (0)           // 0 dB

static uint8_t db256_to_pct(int16_t db256) {
    int32_t clamped = db256;
    if (clamped < VOLUME_DB_MIN) clamped = VOLUME_DB_MIN;
    if (clamped > VOLUME_DB_MAX) clamped = VOLUME_DB_MAX;
    return (uint8_t)((clamped - VOLUME_DB_MIN) * 100 /
                     (VOLUME_DB_MAX - VOLUME_DB_MIN));
}

//--------------------------------------------------------------------
// Descriptor string callback
//--------------------------------------------------------------------

static const char *_get_descriptor_string(uint index) {
    if (index >= 1 && index <= count_of(descriptor_strings)) {
        return descriptor_strings[index - 1];
    }
    return "";
}

//--------------------------------------------------------------------
// Audio reconfigure (called when sample rate changes)
//--------------------------------------------------------------------

static void _audio_reconfigure(void) {
    if (_state.freq != 44100 && _state.freq != 48000) {
        _state.freq = 48000;
    }
    audio_out_set_sample_rate(_state.freq);
    // Update nominal feedback for new rate: (freq << 14) / 1000
    _feedback_value = (_state.freq << 14u) / 1000u;
    printf("USB Audio: sample rate set to %lu Hz\n", (unsigned long)_state.freq);
}

//--------------------------------------------------------------------
// USB audio packet handlers
//--------------------------------------------------------------------

static void _as_audio_packet(struct usb_endpoint *ep) {
    struct usb_buffer *usb_buffer = usb_current_out_packet_buffer(ep);

    uint32_t data_len = usb_buffer->data_len;
    // must be stereo 16-bit frames = multiples of 4 bytes
    if (data_len & 3u) {
        usb_grow_transfer(ep->current_transfer, 1);
        usb_packet_done(ep);
        return;
    }

    int16_t *samples = (int16_t *)usb_buffer->data;
    uint32_t num_samples = data_len / 2;       // total int16_t values
    uint32_t frame_count = data_len / 4;       // stereo frames

    // Track peak levels for VU meter
    for (uint32_t i = 0; i < frame_count; i++) {
        int32_t l = samples[i * 2];
        int32_t r = samples[i * 2 + 1];
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > _peak_left)  _peak_left  = l;
        if (r > _peak_right) _peak_right = r;
    }

    // ASRC: adjust buffer fill toward 50% target to compensate for
    // clock drift between USB host and audio output clock.
    {
        uint32_t buf_count = audio_out_get_buffer_count();
        asrc_update_buffer_level(buf_count, AUDIO_OUT_RING_BUFFER_SIZE);
    }

    // Max output: input + 2% stretch headroom
    static int16_t asrc_out[256];
    uint32_t out_samples = asrc_process(samples, num_samples,
                                        asrc_out, count_of(asrc_out));
    audio_out_write(asrc_out, out_samples);

    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);
}

static void _as_sync_packet(struct usb_endpoint *ep) {
    struct usb_buffer *buffer = usb_current_in_packet_buffer(ep);
    buffer->data_len = 3;

    // Return pre-computed nominal feedback value.
    // ASRC handles actual clock drift compensation on the device side.
    uint32_t feedback = _feedback_value;

    buffer->data[0] = (uint8_t)(feedback);
    buffer->data[1] = (uint8_t)(feedback >> 8u);
    buffer->data[2] = (uint8_t)(feedback >> 16u);

    usb_grow_transfer(ep->current_transfer, 1);
    usb_packet_done(ep);
}

static const struct usb_transfer_type as_transfer_type = {
    .on_packet = _as_audio_packet,
    .initial_packet_count = 1,
};

static const struct usb_transfer_type as_sync_transfer_type = {
    .on_packet = _as_sync_packet,
    .initial_packet_count = 1,
};

static struct usb_transfer as_transfer;
static struct usb_transfer as_sync_transfer;

//--------------------------------------------------------------------
// Control request handlers
//--------------------------------------------------------------------

static bool do_get_current(struct usb_setup_packet *setup) {
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
        USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        switch (setup->wValue >> 8u) {
        case FEATURE_MUTE_CONTROL:
            usb_start_tiny_control_in_transfer(_state.mute, 1);
            return true;
        case FEATURE_VOLUME_CONTROL:
            usb_start_tiny_control_in_transfer(_state.volume, 2);
            return true;
        }
    } else if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
               USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
        if ((setup->wValue >> 8u) == ENDPOINT_FREQ_CONTROL) {
            usb_start_tiny_control_in_transfer(_state.freq, 3);
            return true;
        }
    }
    return false;
}

static bool do_get_minimum(struct usb_setup_packet *setup) {
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
        USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        if ((setup->wValue >> 8u) == FEATURE_VOLUME_CONTROL) {
            usb_start_tiny_control_in_transfer((uint16_t)(int16_t)VOLUME_DB_MIN, 2);
            return true;
        }
    }
    return false;
}

static bool do_get_maximum(struct usb_setup_packet *setup) {
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
        USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        if ((setup->wValue >> 8u) == FEATURE_VOLUME_CONTROL) {
            usb_start_tiny_control_in_transfer((uint16_t)(int16_t)VOLUME_DB_MAX, 2);
            return true;
        }
    }
    return false;
}

static bool do_get_resolution(struct usb_setup_packet *setup) {
    if ((setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK) ==
        USB_REQ_TYPE_RECIPIENT_INTERFACE) {
        if ((setup->wValue >> 8u) == FEATURE_VOLUME_CONTROL) {
            usb_start_tiny_control_in_transfer((uint16_t)(int16_t)(1 * 256), 2);
            return true;
        }
    }
    return false;
}

// Pending set-current command (filled in setup handler, processed in out handler)
static struct {
    uint8_t cmd;
    uint8_t type;
    uint8_t cs;
    uint8_t cn;
    uint8_t unit;
    uint8_t len;
} _audio_cmd;

static void audio_cmd_packet(struct usb_endpoint *ep) {
    struct usb_buffer *buffer = usb_current_out_packet_buffer(ep);
    _audio_cmd.cmd = 0;

    if (buffer->data_len >= _audio_cmd.len) {
        if (_audio_cmd.type == USB_REQ_TYPE_RECIPIENT_INTERFACE) {
            switch (_audio_cmd.cs) {
            case FEATURE_MUTE_CONTROL:
                _state.mute = buffer->data[0];
                audio_out_set_mute(_state.mute);
                printf("USB Audio: mute=%d\n", _state.mute);
                break;
            case FEATURE_VOLUME_CONTROL:
                _state.volume = *(int16_t *)buffer->data;
                _state.volume_pct = db256_to_pct(_state.volume);
                audio_out_set_volume(_state.volume_pct);
                printf("USB Audio: vol=%d dB, %d%%\n",
                       _state.volume / 256, _state.volume_pct);
                break;
            }
        } else if (_audio_cmd.type == USB_REQ_TYPE_RECIPIENT_ENDPOINT) {
            if (_audio_cmd.cs == ENDPOINT_FREQ_CONTROL) {
                uint32_t new_freq = (*(uint32_t *)buffer->data) & 0x00ffffffu;
                if (new_freq && _state.freq != new_freq) {
                    _state.freq = new_freq;
                    _audio_reconfigure();
                }
            }
        }
    }
    usb_start_empty_control_in_transfer_null_completion();
}

static const struct usb_transfer_type _audio_cmd_transfer_type = {
    .on_packet = audio_cmd_packet,
    .initial_packet_count = 1,
};

static bool do_set_current(struct usb_setup_packet *setup) {
    if (setup->wLength && setup->wLength < 64) {
        _audio_cmd.cmd  = AUDIO_REQ_SetCurrent;
        _audio_cmd.type = setup->bmRequestType & USB_REQ_TYPE_RECIPIENT_MASK;
        _audio_cmd.len  = (uint8_t)setup->wLength;
        _audio_cmd.unit = (uint8_t)(setup->wIndex >> 8u);
        _audio_cmd.cs   = (uint8_t)(setup->wValue >> 8u);
        _audio_cmd.cn   = (uint8_t)setup->wValue;
        usb_start_control_out_transfer(&_audio_cmd_transfer_type);
        return true;
    }
    return false;
}

static bool ac_setup_request_handler(__unused struct usb_interface *interface,
                                     struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
        case AUDIO_REQ_SetCurrent:    return do_set_current(setup);
        case AUDIO_REQ_GetCurrent:    return do_get_current(setup);
        case AUDIO_REQ_GetMinimum:    return do_get_minimum(setup);
        case AUDIO_REQ_GetMaximum:    return do_get_maximum(setup);
        case AUDIO_REQ_GetResolution: return do_get_resolution(setup);
        default: break;
        }
    }
    return false;
}

static bool _as_setup_request_handler(__unused struct usb_endpoint *ep,
                                      struct usb_setup_packet *setup) {
    setup = __builtin_assume_aligned(setup, 4);
    if (USB_REQ_TYPE_TYPE_CLASS == (setup->bmRequestType & USB_REQ_TYPE_TYPE_MASK)) {
        switch (setup->bRequest) {
        case AUDIO_REQ_SetCurrent:    return do_set_current(setup);
        case AUDIO_REQ_GetCurrent:    return do_get_current(setup);
        case AUDIO_REQ_GetMinimum:    return do_get_minimum(setup);
        case AUDIO_REQ_GetMaximum:    return do_get_maximum(setup);
        case AUDIO_REQ_GetResolution: return do_get_resolution(setup);
        default: break;
        }
    }
    return false;
}

static bool as_set_alternate(struct usb_interface *interface, uint alt) {
    (void)interface;
    if (alt == 1 && !_streaming) {
        // New stream starting — discard stale samples and reset ASRC state.
        audio_out_clear_buffer();
        asrc_reset();
    }
    _streaming = (alt == 1);
    printf("USB Audio: alt=%u streaming=%d\n", alt, _streaming);
    return alt < 2;
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void usb_audio_init(void) {
    usb_interface_init(&ac_interface, &audio_device_config.ac_interface,
                       NULL, 0, true);
    ac_interface.setup_request_handler = ac_setup_request_handler;

    static struct usb_endpoint *const op_endpoints[] = {
        &ep_op_out, &ep_op_sync,
    };
    usb_interface_init(&as_op_interface, &audio_device_config.as_op_interface,
                       op_endpoints, count_of(op_endpoints), true);
    as_op_interface.set_alternate_handler = as_set_alternate;
    ep_op_out.setup_request_handler = _as_setup_request_handler;

    as_transfer.type = &as_transfer_type;
    usb_set_default_transfer(&ep_op_out, &as_transfer);
    as_sync_transfer.type = &as_sync_transfer_type;
    usb_set_default_transfer(&ep_op_sync, &as_sync_transfer);

    static struct usb_interface *const boot_device_interfaces[] = {
        &ac_interface,
        &as_op_interface,
    };
    __unused struct usb_device *device = usb_device_init(
        &boot_device_descriptor,
        &audio_device_config.descriptor,
        boot_device_interfaces, count_of(boot_device_interfaces),
        _get_descriptor_string);
    assert(device);

    asrc_init(50);
    usb_device_start();

    printf("USB Audio: init done (UAC1, async feedback + ASRC)\n");
}

bool usb_audio_is_streaming(void) {
    return _streaming;
}

uint32_t usb_audio_get_sample_rate(void) {
    return _state.freq;
}

uint8_t usb_audio_get_volume(void) {
    return _state.volume_pct;
}

bool usb_audio_get_mute(void) {
    return _state.mute;
}

void usb_audio_get_levels(uint8_t *left, uint8_t *right) {
    // Scale 16-bit peaks to 0-100, then decay
    uint8_t l = (uint8_t)((_peak_left  * 100) >> 15);
    uint8_t r = (uint8_t)((_peak_right * 100) >> 15);
    if (l > 100) l = 100;
    if (r > 100) r = 100;

    // Decay peaks for smooth VU meter animation
    _peak_left  = _peak_left  * 7 / 8;
    _peak_right = _peak_right * 7 / 8;

    if (left)  *left  = l;
    if (right) *right = r;
}
