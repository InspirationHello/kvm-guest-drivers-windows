#include "precomp.h"

#if defined(EVENT_TRACING)
#include "control.tmh"
#endif

EXTERN_C_START

static
VOID
VioAudioHandleCtrlMsg(
    IN WDFDEVICE Device,
    IN PVBUFFER buf
);

VOID
VioAudioSendCtrlMsg(
    IN WDFDEVICE Device,
    IN PVOID Data,
    IN ULONG Length
)
{
    struct VirtIOBufferDescriptor sg;
    struct virtqueue *vq;
    UINT len;
    PDEVICE_CONTEXT pContext = DeviceGetContext(Device);
    VIRTIO_AUDIO_CONTROL cpkt;

    vq = pContext->CtrlOutVirtQueue;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s vq = %p\n", __FUNCTION__, vq);

    sg.physAddr = MmGetPhysicalAddress(Data);
    sg.length = Length;

    WdfSpinLockAcquire(pContext->CtrlOutQueueLock);
    if (0 <= virtqueue_add_buf(vq, &sg, 1, 0, &cpkt, NULL, 0)) {
        virtqueue_kick(vq);

        while (!virtqueue_get_buf(vq, &len)) {
            LARGE_INTEGER interval;
            interval.QuadPart = -1;
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
        }
    }
    WdfSpinLockRelease(pContext->CtrlOutQueueLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

VOID
VioAudioSendCtrlEvent(
    IN WDFDEVICE Device,
    IN USHORT event,
    IN USHORT value
)
{
    struct VirtIOBufferDescriptor sg;
    struct virtqueue *vq;
    UINT len;
    PDEVICE_CONTEXT pContext = DeviceGetContext(Device);
    VIRTIO_AUDIO_CONTROL cpkt;

    vq = pContext->CtrlOutVirtQueue;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s vq = %p\n", __FUNCTION__, vq);

    cpkt.event = event;
    cpkt.value = value;

    sg.physAddr = MmGetPhysicalAddress(&cpkt);
    sg.length = sizeof(cpkt);

    WdfSpinLockAcquire(pContext->CtrlOutQueueLock);
    if (0 <= virtqueue_add_buf(vq, &sg, 1, 0, &cpkt, NULL, 0)) {
        virtqueue_kick(vq);

        while (!virtqueue_get_buf(vq, &len)){
            LARGE_INTEGER interval;
            interval.QuadPart = -1;
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
        }
    }
    WdfSpinLockRelease(pContext->CtrlOutQueueLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}

#pragma warning (push)
#pragma warning (disable:4706)
VOID
VioAudioCtrlWorkHandler(
    IN WDFDEVICE Device
)
{
    struct virtqueue *vq;
    PVBUFFER buf;
    UINT len;
    NTSTATUS  status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pContext = DeviceGetContext(Device);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = pContext->CtrlInVirtQueue;
    ASSERT(vq);

    WdfSpinLockAcquire(pContext->CtrlInQueueLock);
    while ((buf = (PVBUFFER)virtqueue_get_buf(vq, &len))) {
        WdfSpinLockRelease(pContext->CtrlInQueueLock);

        buf->len = len;
        buf->offset = 0;
        
        VioAudioHandleCtrlMsg(Device, buf);

        WdfSpinLockAcquire(pContext->CtrlInQueueLock);
        status = VioAudioSendVBuf(vq, buf);
        if (!NT_SUCCESS(status)){
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding buffer to queue\n", __FUNCTION__, __LINE__);
            VioAudioFreeBuffer(buf);
        }
    }
    WdfSpinLockRelease(pContext->CtrlInQueueLock);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
}
#pragma warning (pop)

VOID
VioAudioHandleCtrlMsg(
    IN WDFDEVICE Device,
    IN PVBUFFER buf
)
{
    PDEVICE_CONTEXT pContext = DeviceGetContext(Device);
    PVIRTIO_AUDIO_CONTROL cpkt;

    UNREFERENCED_PARAMETER(pContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    cpkt = (PVIRTIO_AUDIO_CONTROL)((ULONG_PTR)buf->va_buf + buf->offset);

    switch (cpkt->event) {
    case VIRTIO_AUDIO_DEVICE_OPEN:
        pContext->HostConnected = !!(cpkt->value);
        break;
    default:
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%s UNKNOWN event = %d\n", __FUNCTION__, cpkt->event);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}


EXTERN_C_END