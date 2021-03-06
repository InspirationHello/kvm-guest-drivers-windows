/*++

Module Name:

    public.h

Abstract:

    This module contains the common declarations shared by driver
    and user applications.

Environment:

    user and kernel

--*/

//
// Define an Interface Guid so that apps can find the device and talk to it.
//

#ifndef VIO_AUDIO_PUBLIC_H_
#define VIO_AUDIO_PUBLIC_H_

#include <initguid.h>

DEFINE_GUID(GUID_DEVINTERFACE_VioAudio,
    0xc45687db, 0xd100, 0x4da4, 0xb7, 0x60, 0x31, 0x59, 0x2e, 0x61, 0x06, 0xaa);
// {c45687db-d100-4da4-b760-31592e6106aa}

enum VIRTIO_AUDIO_DEVICE_EVENT {
    VIRTIO_AUDIO_DEVICE_READY = 0,
    VIRTIO_AUDIO_DEVICE_OPEN,
    VIRTIO_AUDIO_DEVICE_CLOSE,
    VIRTIO_AUDIO_DEVICE_TYPE_OPEN,
    VIRTIO_AUDIO_DEVICE_TYPE_CLOSE,
    VIRTIO_AUDIO_DEVICE_ENABLE,
    VIRTIO_AUDIO_DEVICE_DISABLE,
    VIRTIO_AUDIO_DEVICE_SET_FORMAT,
    VIRTIO_AUDIO_DEVICE_SET_STATE,
    VIRTIO_AUDIO_DEVICE_SET_VOLUME,
    VIRTIO_AUDIO_DEVICE_SET_MUTE,
    VIRTIO_AUDIO_DEVICE_GET_MUTE,
    NR_VIRTIO_AUDIO_DEVICE_EVENT
};

enum VIRTIO_AUDIO_DEVICE_TYPE {
    VIRTIO_AUDIO_PLAYBACK_DEVICE = 0,
    VIRTIO_AUDIO_RECORD_DEVICE
};

typedef struct _VIRTIO_AUDIO_FORMAT {
    UINT16 channels;
    UINT16 bits_per_sample;
    UINT32 samples_per_sec;
    UINT32 avg_bytes_per_sec;
}VIRTIO_AUDIO_FORMAT, *PVIRTIO_AUDIO_FORMAT;

#define IOCTL_VIOAUDIO_DEVICE_OPEN             CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_DEVICE_CLOSE            CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_DEVICE_TYPE_OPEN        CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_DEVICE_TYPE_CLOSE       CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SEND_DATA               CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_GET_DATA                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SET_FORMAT              CTL_CODE(FILE_DEVICE_UNKNOWN, 0x806, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_GET_FORMAT              CTL_CODE(FILE_DEVICE_UNKNOWN, 0x807, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SET_DISABLE             CTL_CODE(FILE_DEVICE_UNKNOWN, 0x808, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SET_STATE               CTL_CODE(FILE_DEVICE_UNKNOWN, 0x809, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SET_VOLUME              CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_SET_MUTE                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_VIOAUDIO_GET_MUTE                CTL_CODE(FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED, FILE_ANY_ACCESS)

#endif // VIO_AUDIO_PUBLIC_H_
