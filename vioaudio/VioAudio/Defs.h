#if !defined(_DEFS_H_)
#define _DEFS_H_

EXTERN_C_START

#include "virtio.h"
#include "public.h"
#include "trace.h"

#define VIRTIO_ID_AUDIO        13 /* virtio audio */

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST    0 /* Tell before reclaiming pages */
#define VIRTIO_BALLOON_F_STATS_VQ    1 /* Memory status virtqueue */

typedef struct _VIRTIO_AUDIO_CONFIG
{
    u32 reserved0;
    u32 reserved1;
}VIRTIO_AUDIO_CONFIG, *PVIRTIO_AUDIO_CONFIG;

#pragma pack (push)
#pragma pack (1)
typedef struct _VIRTIO_AUDIO_CONTROL {
    u16 event;
    u16 value;
}VIRTIO_AUDIO_CONTROL, *PVIRTIO_AUDIO_CONTROL;
#pragma pack (pop)

typedef struct _VBUFFER {
    PHYSICAL_ADDRESS    pa_buf;
    PVOID               va_buf;
    size_t              size;
    size_t              len;
    size_t              offset;
} VBUFFER, *PVBUFFER;

typedef struct _WRITE_BUFFER_ENTRY{
    SINGLE_LIST_ENTRY ListEntry;
    WDFMEMORY         EntryHandle;
    WDFREQUEST        Request;
    PVOID             Buffer;
} WRITE_BUFFER_ENTRY, *PWRITE_BUFFER_ENTRY;


typedef struct virtqueue VIOQUEUE, *PVIOQUEUE;
typedef struct VirtIOBufferDescriptor VIO_SG, *PVIO_SG;

#define __DRIVER_NAME "VIOAUDIO: "

typedef struct {
    SINGLE_LIST_ENTRY       SingleListEntry;
    PMDL                    PageMdl;
} PAGE_LIST_ENTRY, *PPAGE_LIST_ENTRY;

typedef struct _DRIVER_CONTEXT{
    // one global lookaside owned by the driver object
    WDFLOOKASIDE WriteBufferLookaside;
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DRIVER_CONTEXT, DriverGetContext);


typedef struct _DEVICE_CONTEXT {
    WDFINTERRUPT            WdfInterrupt;
    WDFINTERRUPT            QueuesInterrupt;

    VIRTIO_WDF_DRIVER       VioDevice;

    BOOLEAN                 RxVqFull;
    BOOLEAN                 TxVqFull;

    PVIOQUEUE               RxVirtQueue;
    PVIOQUEUE               TxVirtQueue;
    PVIOQUEUE               CtrlInVirtQueue;
    PVIOQUEUE               CtrlOutVirtQueue;

    WDFSPINLOCK             RxQueueLock;
    WDFSPINLOCK             TxQueueLock;
    WDFSPINLOCK             CtrlInQueueLock;
    WDFSPINLOCK             CtrlOutQueueLock;

    PVBUFFER                RxBuffer;
    WDFREQUEST              PendingReadRequest;

    KEVENT                  HostAckEvent;

    NPAGED_LOOKASIDE_LIST   LookAsideList;
    BOOLEAN                 bListInitialized;

    BOOLEAN                 bShutDown;
    BOOLEAN                 HostConnected;
    BOOLEAN                 GuestConnected;

    KEVENT                  WakeUpThread;
    PKTHREAD                Thread;

    WDFQUEUE                ReadQueue;
    WDFQUEUE                WriteQueue;
    WDFQUEUE                IoctlQueue;

    SINGLE_LIST_ENTRY       WriteBuffersList;

} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, DeviceGetContext);

#define VIOAUDIO_MGMT_POOL_TAG 'mtaV'

#ifndef _IRQL_requires_
#define _IRQL_requires_(level)
#endif

EVT_WDF_DRIVER_DEVICE_ADD VioAudioDeviceAdd;
KSTART_ROUTINE            VioAudioRoutine;
DRIVER_INITIALIZE DriverEntry;

// Context cleanup callbacks generally run at IRQL <= DISPATCH_LEVEL but
// WDFDRIVER and WDFDEVICE cleanup is guaranteed to run at PASSIVE_LEVEL.
// Annotate the prototypes to make static analysis happy.
EVT_WDF_OBJECT_CONTEXT_CLEANUP                 _IRQL_requires_(PASSIVE_LEVEL) VioAudioEvtDriverContextCleanup;
EVT_WDF_DEVICE_CONTEXT_CLEANUP                 _IRQL_requires_(PASSIVE_LEVEL) VioAudioEvtDeviceContextCleanup;

EVT_WDF_DEVICE_PREPARE_HARDWARE                VioAudioEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE                VioAudioEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY                        VioAudioEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT                         VioAudioEvtDeviceD0Exit;
EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED VioAudioEvtDeviceD0ExitPreInterruptsDisabled;
EVT_WDF_INTERRUPT_ISR                          VioAudioInterruptIsr;
EVT_WDF_INTERRUPT_DPC                          VioAudioInterruptDpc;
EVT_WDF_INTERRUPT_ENABLE                       VioAudioInterruptEnable;
EVT_WDF_INTERRUPT_DISABLE                      VioAudioInterruptDisable;

VOID
VioAudioInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
    );

BOOLEAN
VioAudioInterruptIsr(
    IN WDFINTERRUPT Interrupt,
    IN ULONG        MessageID
    );

VOID
VioAudioQueuesInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
);

NTSTATUS
VioAudioInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
VioAudioInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
    );

NTSTATUS
VioAudioInit(
    IN WDFOBJECT    WdfDevice
    );

VOID
VioAudioTerm(
    IN WDFOBJECT    WdfDevice
    );

VOID
VioAudioTellHost(
    IN WDFOBJECT WdfDevice,
    IN PVIOQUEUE vq
    );

__inline
VOID
EnableInterrupt(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFCONTEXT Context
    )
{
    PDEVICE_CONTEXT devCtx = (PDEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    virtqueue_enable_cb(devCtx->RxVirtQueue);
    virtqueue_kick(devCtx->RxVirtQueue);
    virtqueue_enable_cb(devCtx->TxVirtQueue);
    virtqueue_kick(devCtx->TxVirtQueue);

    if (devCtx->CtrlInVirtQueue){
       virtqueue_enable_cb(devCtx->CtrlInVirtQueue);
       virtqueue_kick(devCtx->CtrlInVirtQueue);
    }

    if (devCtx->CtrlOutVirtQueue) {
        virtqueue_enable_cb(devCtx->CtrlOutVirtQueue);
        virtqueue_kick(devCtx->CtrlOutVirtQueue);
    }
}

__inline
VOID
DisableInterrupt(
    IN PDEVICE_CONTEXT devCtx
    )
{
    virtqueue_disable_cb(devCtx->RxVirtQueue);
    virtqueue_disable_cb(devCtx->TxVirtQueue);

    if (devCtx->CtrlInVirtQueue){
        virtqueue_disable_cb(devCtx->CtrlInVirtQueue);
    }

    if (devCtx->CtrlOutVirtQueue) {
        virtqueue_disable_cb(devCtx->CtrlOutVirtQueue);
    }
}

NTSTATUS
VioAudioCloseWorkerThread(
    IN WDFDEVICE  Device
    );

VOID
VioAudioRoutine(
    IN PVOID pContext
    );

NTSTATUS
VioAudioSendMsgBlock(
    IN struct virtqueue *vq,
    IN PVOID buf,
    IN UINT length);

PVBUFFER
VioAudioAllocateBuffer(
    IN size_t buf_size
);

VOID
VioAudioFreeBuffer(
    IN PVBUFFER buf
);

NTSTATUS
VioAudioSendVBuf(
    IN struct virtqueue *vq,
    IN PVBUFFER buf
);

PVOID
VioAudioGetVBuf(
    IN struct virtqueue *vq
);

size_t 
VioAudioSendWriteBufferEntry(
    IN PDEVICE_CONTEXT DeviceContext,
    IN PWRITE_BUFFER_ENTRY Entry,
    IN size_t Length
);

SSIZE_T
VioAudioFillReadBufLocked(
    IN PDEVICE_CONTEXT DeviceContext,
    IN PVOID outbuf,
    IN SIZE_T count
);

VOID
VioAudioProcessInputBuffers(
    IN PDEVICE_CONTEXT DeviceContext
);

BOOLEAN
VioAudioReclaimConsumedBuffers(
    IN PDEVICE_CONTEXT DeviceContext
);


VOID
VioAudioSendCtrlMsg(
    IN WDFDEVICE Device,
    IN PVOID Data,
    IN ULONG Length
);

VOID
VioAudioSendCtrlEvent(
    IN WDFDEVICE Device,
    IN USHORT event,
    IN USHORT value
);

VOID
VioAudioCtrlWorkHandler(
    IN WDFDEVICE Device
);

VOID
VioAudioDrainQueue(
    IN struct virtqueue *vq
);


VOID
VioAudioEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
);

VOID
VioAudioEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
);

VOID
VioAudioEvtIoRead(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
);

VOID VioAudioEvtReadIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags);

VOID VioAudioEvtIoWrite(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length);

VOID VioAudioEvtWriteIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags);

BOOLEAN
VioAudioPortHasDataLocked(
    IN PDEVICE_CONTEXT devCtx
);

VOID
VioAudioDiscardDataLocked(
    IN PDEVICE_CONTEXT devCtx
);

BOOLEAN
VioAudioWillBlockWrite(
    IN PDEVICE_CONTEXT devCtx
);

VOID
VioAudioDeviceCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
);

VOID
VioAudioDeviceClose(
    IN WDFFILEOBJECT FileObject
);

EXTERN_C_END

#endif  // _PROTOTYPES_H_
