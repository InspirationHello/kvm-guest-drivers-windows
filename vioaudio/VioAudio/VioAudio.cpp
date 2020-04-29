#ifndef _VIO_AUDIO_H_
#define _VIO_AUDIO_H_

#include "precomp.h"

#if defined(EVENT_TRACING)
#include "vioaudio.tmh"
#endif

#pragma warning (push)
#pragma warning (disable:4706)
VOID
VioAudioDrainQueue(
    IN struct virtqueue *vq
)
{
    PVBUFFER buf;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    while (buf = (PVBUFFER)virtqueue_detach_unused_buf(vq))  {
        VioAudioFreeBuffer(buf);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);
}
#pragma warning (pop)

NTSTATUS
VioAudioFillQueue(
    IN struct virtqueue *vq,
    IN WDFSPINLOCK Lock
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVBUFFER buf = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "--> %s\n", __FUNCTION__);

    for ( ; ; ) {
        buf = VioAudioAllocateBuffer(PAGE_SIZE);
        if (buf == NULL) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT, "VioAudioAllocateBuffer failed\n");
            WdfSpinLockAcquire(Lock);
            VioAudioDrainQueue(vq);
            WdfSpinLockRelease(Lock);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        WdfSpinLockAcquire(Lock);
        status = VioAudioSendVBuf(vq, buf);
        if (!NT_SUCCESS(status)) {
            VioAudioFreeBuffer(buf);
            WdfSpinLockRelease(Lock);
            break;
        }
        WdfSpinLockRelease(Lock);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_INIT, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

NTSTATUS
VioAudioInit(
    IN WDFOBJECT    WdfDevice
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = DeviceGetContext(WdfDevice);
    u64 u64HostFeatures;
    u64 u64GuestFeatures = 0;
    bool notify_stat_queue = false;
    VIRTIO_WDF_QUEUE_PARAM params[4];
    PVIOQUEUE vqs[4];
    ULONG nvqs;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> VioAudioInit\n");

    WdfObjectAcquireLock(WdfDevice);

    // in
    params[0].Interrupt = devCtx->QueuesInterrupt;

    // out
    params[1].Interrupt = devCtx->QueuesInterrupt;

    // ctrl in
    params[2].Interrupt = devCtx->WdfInterrupt;

    // ctrl out
    params[3].Interrupt = devCtx->WdfInterrupt;

    u64HostFeatures = VirtIOWdfGetDeviceFeatures(&devCtx->VioDevice);

    nvqs = 4;

#if 0
    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_F_VERSION_1)) {
        virtio_feature_enable(u64GuestFeatures, VIRTIO_F_VERSION_1);
    }

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_F_ANY_LAYOUT)) {
        virtio_feature_enable(u64GuestFeatures, VIRTIO_F_ANY_LAYOUT);
    }

    if (virtio_is_feature_enabled(u64HostFeatures, VIRTIO_BALLOON_F_STATS_VQ)) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
            "Enable stats feature.\n");

        virtio_feature_enable(u64GuestFeatures, VIRTIO_BALLOON_F_STATS_VQ);
        nvqs = 4;
    } else {
        nvqs = 2;
    }
#endif

    status = VirtIOWdfSetDriverFeatures(&devCtx->VioDevice, u64GuestFeatures);
    if (NT_SUCCESS(status)) {
        // initialize 2 or 4 queues
        status = VirtIOWdfInitQueues(&devCtx->VioDevice, nvqs, vqs, params);
        if (NT_SUCCESS(status)) {

            devCtx->RxVirtQueue = vqs[0];
            devCtx->TxVirtQueue = vqs[1];

            if (nvqs == 4) {

                devCtx->CtrlInVirtQueue = vqs[2];
                devCtx->CtrlOutVirtQueue = vqs[3];
            }

            status = VioAudioFillQueue(devCtx->RxVirtQueue, devCtx->RxQueueLock);
            if (!NT_SUCCESS(status)){
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                    "VioAudioFillQueue RxVirtQueue failed with %x\n", status);
            }

            status = VioAudioFillQueue(devCtx->CtrlInVirtQueue, devCtx->CtrlInQueueLock);
            if (NT_SUCCESS(status)) {
                VirtIOWdfSetDriverOK(&devCtx->VioDevice);
            } else {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                    "VioAudioFillQueue CtrlInVirtQueue failed with %x\n", status);

                VirtIOWdfSetDriverFailed(&devCtx->VioDevice);
            }

        } else {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
                "VirtIOWdfInitQueues failed with %x\n", status);
            VirtIOWdfSetDriverFailed(&devCtx->VioDevice);
        }
    } else {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "VirtIOWdfSetDriverFeatures failed with %x\n", status);
        VirtIOWdfSetDriverFailed(&devCtx->VioDevice);
    }

    // notify the stat queue only after the virtual device has been fully initialized
    if (notify_stat_queue) {
        virtqueue_kick(devCtx->CtrlInVirtQueue);
    }

    WdfObjectReleaseLock(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- VioAudioInit\n");
    return status;
}


VOID
VioAudioTerm(
    IN WDFOBJECT    WdfDevice
)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> VioAudioTerm\n");

    WdfObjectAcquireLock(WdfDevice);

    VioAudioDrainQueue(devCtx->CtrlInVirtQueue);

    VirtIOWdfDestroyQueues(&devCtx->VioDevice);
    devCtx->CtrlInVirtQueue = NULL;
    devCtx->CtrlOutVirtQueue = NULL;

    WdfObjectReleaseLock(WdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- VioAudioTerm\n");
}

BOOLEAN
VioAudioPortHasDataLocked(
    IN PDEVICE_CONTEXT devCtx
)
{
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    if (devCtx->RxBuffer) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }

    devCtx->RxBuffer = (PVBUFFER)VioAudioGetVBuf(devCtx->RxVirtQueue);
    if (devCtx->RxBuffer) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d\n", __FUNCTION__, __LINE__);
        return TRUE;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<--%s::%d first_unused %d, last_used %d, index %d, num_unused %d\n", __FUNCTION__, __LINE__, 
        devCtx->RxVirtQueue->first_unused, devCtx->RxVirtQueue->last_used, 
        devCtx->RxVirtQueue->index, devCtx->RxVirtQueue->num_unused);
    
    return FALSE;
}

VOID
VioAudioDiscardDataLocked(
    IN PDEVICE_CONTEXT devCtx
)
{
    struct virtqueue *vq;
    PVBUFFER buf = NULL;
    UINT len;
    NTSTATUS  status = STATUS_SUCCESS;
    UINT ret = 0;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    vq = devCtx->RxVirtQueue;

    if (devCtx->RxBuffer) {
        buf = devCtx->RxBuffer;
    }
    else if (vq) {
        buf = (PVBUFFER)virtqueue_get_buf(vq, &len);
    }

    while (buf) {
        status = VioAudioSendVBuf(vq, buf);
        if (!NT_SUCCESS(status)) {
            ++ret;
            VioAudioFreeBuffer(buf);
        }

        buf = (PVBUFFER)virtqueue_get_buf(vq, &len);
    }

    devCtx->RxBuffer = NULL;
    if (ret > 0) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "%s::%d Error adding %u buffers back to queue\n",
            __FUNCTION__, __LINE__, ret);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
}



BOOLEAN
VioAudioWillBlockWrite(
    IN PDEVICE_CONTEXT devCtx
)
{
    BOOLEAN ret = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);
    if (!devCtx->HostConnected) {
        return TRUE;
    }

    ret = VioAudioReclaimConsumedBuffers(devCtx);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return ret;
}

VOID
VioAudioTellHost(
    IN WDFOBJECT WdfDevice,
    IN PVIOQUEUE vq
)
{
    VIO_SG              sg;
    PDEVICE_CONTEXT     devCtx = DeviceGetContext(WdfDevice);
    NTSTATUS            status;
    LARGE_INTEGER       timeout = { 0 };
    bool                do_notify;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(sg);
    UNREFERENCED_PARAMETER(status);
    UNREFERENCED_PARAMETER(timeout);
    UNREFERENCED_PARAMETER(do_notify);
    UNREFERENCED_PARAMETER(vq);
    UNREFERENCED_PARAMETER(devCtx);

#if 0
    sg.physAddr = MmGetPhysicalAddress(devCtx->pfns_table);
    sg.length = sizeof(devCtx->pfns_table[0]) * devCtx->num_pfns;

    WdfSpinLockAcquire(devCtx->InfDefQueueLock);
    if (virtqueue_add_buf(vq, &sg, 1, 0, devCtx, NULL, 0) < 0)
    {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS, "<-> %s :: Cannot add buffer\n", __FUNCTION__);
        WdfSpinLockRelease(devCtx->InfDefQueueLock);
        return;
    }
    do_notify = virtqueue_kick_prepare(vq);
    WdfSpinLockRelease(devCtx->InfDefQueueLock);

    if (do_notify)
    {
        virtqueue_notify(vq);
    }

    timeout.QuadPart = Int32x32To64(1000, -10000);
    status = KeWaitForSingleObject(
        &devCtx->HostAckEvent,
        Executive,
        KernelMode,
        FALSE,
        &timeout);
    ASSERT(NT_SUCCESS(status));
    if (STATUS_TIMEOUT == status)
    {
        TraceEvents(TRACE_LEVEL_WARNING, DBG_HW_ACCESS, "<--> TimeOut\n");
    }
#endif
}


VOID
VioAudioDeviceCreate(
    IN WDFDEVICE WdfDevice,
    IN WDFREQUEST Request,
    IN WDFFILEOBJECT FileObject
)
{
    PDEVICE_CONTEXT deviceContext;
    NTSTATUS        status = STATUS_SUCCESS;

    deviceContext = DeviceGetContext(WdfDevice);

    UNREFERENCED_PARAMETER(FileObject);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%!FUNC! Entry");

    if (deviceContext->GuestConnected == TRUE) {
        TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP,
            "Guest already connected\n");
        status = STATUS_OBJECT_NAME_EXISTS;
    } else {
        deviceContext->GuestConnected = TRUE;

        VioAudioReclaimConsumedBuffers(deviceContext);

        VioAudioSendCtrlEvent(WdfDevice, VIRTIO_AUDIO_DEVICE_OPEN, 1);
    }

    WdfRequestComplete(Request, status);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%!FUNC! Exit");
}

VOID
VioAudioDeviceClose(
    IN WDFFILEOBJECT FileObject
)
{
    WDFDEVICE wdfDevice;
    PDEVICE_CONTEXT deviceContext;

    wdfDevice = WdfFileObjectGetDevice(FileObject);
    deviceContext = DeviceGetContext(wdfDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%!FUNC! Entry");

    if (deviceContext->GuestConnected) {
        VioAudioSendCtrlEvent(wdfDevice, VIRTIO_AUDIO_DEVICE_OPEN, 0);
    }

    //WdfSpinLockAcquire(deviceContext->RxQueueLock);
    //VioAudioDiscardDataLocked(deviceContext);
    //WdfSpinLockRelease(deviceContext->RxQueueLock);

    VioAudioReclaimConsumedBuffers(deviceContext);

    deviceContext->GuestConnected = FALSE;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "%!FUNC! Exit");
}


#endif // !_VIO_AUDIO_H_
