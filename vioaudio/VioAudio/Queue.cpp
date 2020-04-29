/*++

Module Name:

    queue.c

Abstract:

    This file contains the queue entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "queue.tmh"
#endif

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VioAudioQueueInitialize)
#endif

#pragma warning (push)
#pragma warning (disable:4505)
static NTSTATUS ioctl_request_data(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    // WDFQUEUE ioQueue = WdfRequestGetIoQueue(Request);

    UNREFERENCED_PARAMETER(DeviceContext);

    char *out = NULL;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    static char test_data[] = "hello, I'm test data haha.";
    size_t size = min(sizeof(test_data), OutputBufferLength);

    status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, (PVOID *)&out, NULL);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_QUEUE,
            "%!FUNC! Failed to retrieve the output buffer %!STATUS!\n", status);
        return STATUS_INVALID_USER_BUFFER;
    }

    RtlCopyMemory(out, test_data, size);
    *BytesReturned = size;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_send_data(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    PVOID    buffer = NULL;
    size_t   buffSize = 0;
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    status = WdfRequestRetrieveInputBuffer(Request, OutputBufferLength, &buffer, &buffSize);
    if (!NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not get request memory buffer %!STATUS!\n",
            status);
        return STATUS_INVALID_USER_BUFFER;
    }

    WdfSpinLockAcquire(DeviceContext->TxQueueLock);
    status = VioAudioSendMsgBlock(DeviceContext->TxVirtQueue, buffer, buffSize);
    WdfSpinLockRelease(DeviceContext->TxQueueLock);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not send buffer to host %!STATUS!\n",
            status);
        return status;
    }

    if (BytesReturned) {
        *BytesReturned = buffSize;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}
#pragma warning (pop)

static NTSTATUS ioctl_set_format(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned
)
{
    PVOID                  buffer = NULL, ctrlPkgbuffer = NULL;
    size_t                 buffSize = 0;
    NTSTATUS               status = STATUS_SUCCESS;
    WDFDEVICE              wdfDevice;
    PVIRTIO_AUDIO_CONTROL  cpkt;
    ULONG                  cpktLength;

    UNREFERENCED_PARAMETER(DeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    wdfDevice = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));

    status = WdfRequestRetrieveInputBuffer(Request, OutputBufferLength, &buffer, &buffSize);
    if (!NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not get request memory buffer %!STATUS!\n",
            status);
        return STATUS_INVALID_USER_BUFFER;
    }

    cpktLength = sizeof(VIRTIO_AUDIO_CONTROL) + buffSize;

    ctrlPkgbuffer = ExAllocatePoolWithTag(NonPagedPool, cpktLength,
        VIOAUDIO_MGMT_POOL_TAG);
    if (ctrlPkgbuffer == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Failed to allocate buffer.\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    cpkt = (PVIRTIO_AUDIO_CONTROL)ctrlPkgbuffer;

    cpkt->event = VIRTIO_AUDIO_DEVICE_SET_FORMAT;
    cpkt->value = 1;

    RtlCopyMemory((UINT8*)ctrlPkgbuffer + sizeof(VIRTIO_AUDIO_CONTROL), buffer, buffSize);

    VioAudioSendCtrlMsg(wdfDevice, cpkt, cpktLength);

    ExFreePoolWithTag(ctrlPkgbuffer, VIOAUDIO_MGMT_POOL_TAG);

    if (BytesReturned) {
        *BytesReturned = buffSize;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

#if 0
typedef NTSTATUS(*send_event_common_pft)(WDFDEVICE wdfDevice, PVOID Buffer, size_t Size, size_t *BytesReturned);

static NTSTATUS ioctl_send_event_common(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned,
    send_event_common_pft callback
)
{
    PVOID                  buffer = NULL;
    size_t                 buffSize = 0;
    NTSTATUS               status = STATUS_SUCCESS;
    WDFDEVICE              wdfDevice;

    UNREFERENCED_PARAMETER(DeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    wdfDevice = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));

    status = WdfRequestRetrieveInputBuffer(Request, OutputBufferLength, &buffer, &buffSize);
    if (!NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not get request memory buffer %!STATUS!\n",
            status);
        return STATUS_INVALID_USER_BUFFER;
    }

    if (callback) {
        status = callback(wdfDevice, buffer, buffSize, BytesReturned);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return status;
}
#endif

static NTSTATUS ioctl_send_event(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned,
    USHORT                EventType
)
{
    PVOID                  buffer = NULL;
    size_t                 buffSize = 0;
    NTSTATUS               status = STATUS_SUCCESS;
    WDFDEVICE              wdfDevice;
    VIRTIO_AUDIO_CONTROL   cpkt = { 0 };
    INT                   *pDisable;

    UNREFERENCED_PARAMETER(DeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    wdfDevice = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));

    status = WdfRequestRetrieveInputBuffer(Request, OutputBufferLength, &buffer, &buffSize);
    if (!NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not get request memory buffer %!STATUS!\n",
            status);
        return STATUS_INVALID_USER_BUFFER;
    }

    pDisable = (INT*)buffer;

    VioAudioSendCtrlEvent(wdfDevice, EventType, (USHORT)*pDisable);

    if (BytesReturned) {
        *BytesReturned = buffSize;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

static NTSTATUS ioctl_send_event_int32(
    const PDEVICE_CONTEXT DeviceContext,
    const size_t          OutputBufferLength,
    const WDFREQUEST      Request,
    size_t              * BytesReturned,
    USHORT                EventType
)
{
    PVOID                  buffer = NULL;
    UINT8                  ctrlPkgbuffer[sizeof(VIRTIO_AUDIO_CONTROL) + sizeof(INT32)];
    size_t                 buffSize = 0, cpktLength;
    NTSTATUS               status = STATUS_SUCCESS;
    WDFDEVICE              wdfDevice;
    PVIRTIO_AUDIO_CONTROL  cpkt;

    UNREFERENCED_PARAMETER(DeviceContext);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    wdfDevice = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));

    status = WdfRequestRetrieveInputBuffer(Request, OutputBufferLength, &buffer, &buffSize);
    if (!NT_SUCCESS(status) || (buffer == NULL)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Could not get request memory buffer %!STATUS!\n",
            status);
        return STATUS_INVALID_USER_BUFFER;
    }

    cpktLength = ARRAYSIZE(ctrlPkgbuffer);
    cpkt = (PVIRTIO_AUDIO_CONTROL)ctrlPkgbuffer;

    cpkt->event = EventType;
    cpkt->value = 1;

    RtlCopyMemory((UINT8*)ctrlPkgbuffer + sizeof(VIRTIO_AUDIO_CONTROL), buffer, sizeof(INT32));

    VioAudioSendCtrlMsg(wdfDevice, cpkt, cpktLength);

    if (BytesReturned) {
        *BytesReturned = buffSize;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return STATUS_SUCCESS;
}

NTSTATUS
VioAudioQueueInitialize(
    _In_ WDFDEVICE Device
    )
/*++

Routine Description:

     The I/O dispatch callbacks for the frameworks device object
     are configured in this function.

     A single default I/O Queue is configured for parallel request
     processing, and a driver context memory allocation is created
     to hold our structure QUEUE_CONTEXT.

Arguments:

    Device - Handle to a framework device object.

Return Value:

    VOID

--*/
{
    NTSTATUS status;
    WDF_IO_QUEUE_CONFIG queueConfig;
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    //
    // Configure a default queue so that requests that are not
    // configure-fowarded using WdfDeviceConfigureRequestDispatching to goto
    // other queues get dispatched here.
    //
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
         &queueConfig,
        WdfIoQueueDispatchParallel
        );

    queueConfig.EvtIoDeviceControl = VioAudioEvtIoDeviceControl;
    queueConfig.EvtIoStop = VioAudioEvtIoStop;

    status = WdfIoQueueCreate(
                 Device,
                 &queueConfig,
                 WDF_NO_OBJECT_ATTRIBUTES,
                 &deviceContext->IoctlQueue
                 );

    if(!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate IoctlQueue failed %!STATUS!", status);
        return status;
    }

#if 0
    status = WdfDeviceConfigureRequestDispatching(
        Device,
        deviceContext->IoctlQueue,
        WdfRequestTypeDeviceControl
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "DeviceConfigureRequestDispatching failed (IoCtrl Queue): %!STATUS!",
            status);
        return status;
    }
#endif


    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,
        WdfIoQueueDispatchSequential);

    queueConfig.EvtIoRead = VioAudioEvtIoRead;
    queueConfig.EvtIoStop = VioAudioEvtReadIoStop;
    status = WdfIoQueueCreate(Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &deviceContext->ReadQueue
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate (Read Queue) failed %!STATUS!", 
            status);
        return status;
    }

    status = WdfDeviceConfigureRequestDispatching(
        Device,
        deviceContext->ReadQueue,
        WdfRequestTypeRead
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "DeviceConfigureRequestDispatching failed (Read Queue): %!STATUS!",
            status);
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.AllowZeroLengthRequests = WdfFalse;
    queueConfig.EvtIoWrite = VioAudioEvtIoWrite;
    queueConfig.EvtIoStop = VioAudioEvtWriteIoStop;

    status = WdfIoQueueCreate(Device, &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES, &deviceContext->WriteQueue);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "WdfIoQueueCreate failed (Write Queue): %!STATUS!",
            status);
        return status;
    }
    status = WdfDeviceConfigureRequestDispatching(
        Device,
        deviceContext->WriteQueue,
        WdfRequestTypeWrite
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "DeviceConfigureRequestDispatching failed (Write Queue): %!STATUS!",
            status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");

    return status;
}

VOID
VioAudioEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
/*++

Routine Description:

    This event is invoked when the framework receives IRP_MJ_DEVICE_CONTROL request.

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    OutputBufferLength - Size of the output buffer in bytes

    InputBufferLength - Size of the input buffer in bytes

    IoControlCode - I/O control code.

Return Value:

    VOID

--*/
{
    WDFDEVICE hDevice = WdfIoQueueGetDevice(Queue);
    PDEVICE_CONTEXT deviceContext = DeviceGetContext(hDevice);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    size_t bytesReturned = 0;
    BOOLEAN needComplete = TRUE;

    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p OutputBufferLength %d InputBufferLength %d IoControlCode %d", 
                Queue, Request, (int) OutputBufferLength, (int) InputBufferLength, IoControlCode);

    switch (IoControlCode)
    {
    case IOCTL_VIOAUDIO_DEVICE_TYPE_OPEN:
        status = ioctl_send_event(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_TYPE_OPEN);
        break;
    case IOCTL_VIOAUDIO_DEVICE_TYPE_CLOSE:
        status = ioctl_send_event(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_TYPE_CLOSE);
        break;
    case IOCTL_VIOAUDIO_SEND_DATA:
        status = ioctl_send_data(deviceContext, OutputBufferLength, Request, &bytesReturned);
        break;
    case IOCTL_VIOAUDIO_GET_DATA:
        // status = ioctl_request_data(deviceContext, OutputBufferLength, Request, &bytesReturned);
        VioAudioEvtIoRead(Queue, Request, OutputBufferLength);
        needComplete = FALSE;
        break;
    case IOCTL_VIOAUDIO_SET_FORMAT:
        status = ioctl_set_format(deviceContext, OutputBufferLength, Request, &bytesReturned);
        break;
    case IOCTL_VIOAUDIO_GET_FORMAT:
        break;
    case IOCTL_VIOAUDIO_SET_DISABLE:
        status = ioctl_send_event(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_DISABLE);
        break;
    case IOCTL_VIOAUDIO_SET_STATE:
        status = ioctl_send_event(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_SET_STATE);
        break;
    case IOCTL_VIOAUDIO_SET_VOLUME:
        status = ioctl_send_event_int32(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_SET_VOLUME);
        break;
    case IOCTL_VIOAUDIO_SET_MUTE:
        status = ioctl_send_event(deviceContext, OutputBufferLength, Request, &bytesReturned, VIRTIO_AUDIO_DEVICE_SET_MUTE);
        break;
    default:
        break;
    }

    if (needComplete) {
        // WdfRequestComplete(Request, status);
        WdfRequestCompleteWithInformation(Request, status, bytesReturned);
    }
    
    return;
}

VOID
VioAudioEvtIoStop(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ ULONG ActionFlags
)
/*++

Routine Description:

    This event is invoked for a power-managed queue before the device leaves the working state (D0).

Arguments:

    Queue -  Handle to the framework queue object that is associated with the
             I/O request.

    Request - Handle to a framework request object.

    ActionFlags - A bitwise OR of one or more WDF_REQUEST_STOP_ACTION_FLAGS-typed flags
                  that identify the reason that the callback function is being called
                  and whether the request is cancelable.

Return Value:

    VOID

--*/
{
    TraceEvents(TRACE_LEVEL_INFORMATION, 
                TRACE_QUEUE, 
                "%!FUNC! Queue 0x%p, Request 0x%p ActionFlags %d", 
                Queue, Request, ActionFlags);

    //
    // In most cases, the EvtIoStop callback function completes, cancels, or postpones
    // further processing of the I/O request.
    //
    // Typically, the driver uses the following rules:
    //
    // - If the driver owns the I/O request, it calls WdfRequestUnmarkCancelable
    //   (if the request is cancelable) and either calls WdfRequestStopAcknowledge
    //   with a Requeue value of TRUE, or it calls WdfRequestComplete with a
    //   completion status value of STATUS_SUCCESS or STATUS_CANCELLED.
    //
    //   Before it can call these methods safely, the driver must make sure that
    //   its implementation of EvtIoStop has exclusive access to the request.
    //
    //   In order to do that, the driver must synchronize access to the request
    //   to prevent other threads from manipulating the request concurrently.
    //   The synchronization method you choose will depend on your driver's design.
    //
    //   For example, if the request is held in a shared context, the EvtIoStop callback
    //   might acquire an internal driver lock, take the request from the shared context,
    //   and then release the lock. At this point, the EvtIoStop callback owns the request
    //   and can safely complete or requeue the request.
    //
    // - If the driver has forwarded the I/O request to an I/O target, it either calls
    //   WdfRequestCancelSentRequest to attempt to cancel the request, or it postpones
    //   further processing of the request and calls WdfRequestStopAcknowledge with
    //   a Requeue value of FALSE.
    //
    // A driver might choose to take no action in EvtIoStop for requests that are
    // guaranteed to complete in a small amount of time.
    //
    // In this case, the framework waits until the specified request is complete
    // before moving the device (or system) to a lower power state or removing the device.
    // Potentially, this inaction can prevent a system from entering its hibernation state
    // or another low system power state. In extreme cases, it can cause the system
    // to crash with bugcheck code 9F.
    //

    return;
}


VOID
VioAudioReadRequestCancel(
    IN WDFREQUEST Request
)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT DeviceContext;

    Device = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    DeviceContext = DeviceGetContext(Device);

    TraceEvents(TRACE_LEVEL_ERROR, DBG_WRITE, "-->%s called on request 0x%p\n", __FUNCTION__, Request);

    // synchronize with VIOSerialQueuesInterruptDpc because the pending
    // request is not guaranteed to be alive after we return from this callback
    WdfSpinLockAcquire(DeviceContext->RxQueueLock);
    DeviceContext->PendingReadRequest = NULL;
    WdfSpinLockRelease(DeviceContext->RxQueueLock);

    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_WRITE, "<-- %s\n", __FUNCTION__);
}

VOID
VioAudioEvtIoRead(
    IN WDFQUEUE   Queue,
    IN WDFREQUEST Request,
    IN size_t     Length
)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT DeviceContext;
    size_t             length;
    NTSTATUS           status;
    PVOID              systemBuffer;

    Device = WdfIoQueueGetDevice(Queue);
    DeviceContext = DeviceGetContext(Device);

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(Length);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &systemBuffer, &length);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfSpinLockAcquire(DeviceContext->RxQueueLock);

    if (!VioAudioPortHasDataLocked(DeviceContext)) {
        if (!DeviceContext->HostConnected) {
            status = STATUS_INSUFFICIENT_RESOURCES;
            length = 0;
        } else {
            ASSERT(DeviceContext->PendingReadRequest == NULL);
            status = WdfRequestMarkCancelableEx(Request,
                VioAudioReadRequestCancel);
            if (!NT_SUCCESS(status)) {
                length = 0;
            } else {
                DeviceContext->PendingReadRequest = Request;
                Request = NULL;
            }
        }
    } else {
        length = (ULONG)VioAudioFillReadBufLocked(DeviceContext, systemBuffer, length);

        if (!length) {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    WdfSpinLockRelease(DeviceContext->RxQueueLock);

    if (Request != NULL) {
        // we are completing the request right here, either because of
        // an error or because data was available in the input buffer
        WdfRequestCompleteWithInformation(Request, status, (ULONG_PTR)length);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");
}

VOID VioAudioEvtReadIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags)
{
    UNREFERENCED_PARAMETER(Queue);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");


    if (ActionFlags & WdfRequestStopActionSuspend) {
        WdfRequestStopAcknowledge(Request, FALSE);
    }
    else if (ActionFlags & WdfRequestStopActionPurge) {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED) {
            WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");
}


VOID VioAudioWriteRequestCancel(IN WDFREQUEST Request)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT DeviceContext;
    PSINGLE_LIST_ENTRY iter;

    Device = WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request));
    DeviceContext = DeviceGetContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    // synchronize with VIOSerialReclaimConsumedBuffers because the pending
    // request is not guaranteed to be alive after we return from this callback
    WdfSpinLockAcquire(DeviceContext->RxQueueLock);
    iter = &DeviceContext->WriteBuffersList;
    while ((iter = iter->Next) != NULL) {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter, WRITE_BUFFER_ENTRY, ListEntry);

        if (entry->Request == Request) {
            entry->Request = NULL;
            break;
        }
    }
    WdfSpinLockRelease(DeviceContext->RxQueueLock);

    WdfRequestCompleteWithInformation(Request, STATUS_CANCELLED, 0L);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");
}


VOID VioAudioEvtIoWrite(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN size_t Length)
{
    NTSTATUS status = STATUS_SUCCESS;
    PVOID InBuf;
    PVOID buffer;
    WDFDEVICE Device;
    PDRIVER_CONTEXT Context;
    PDEVICE_CONTEXT DeviceContext;
    PWRITE_BUFFER_ENTRY entry;
    WDFMEMORY EntryHandle;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    PAGED_CODE();

    Device = WdfIoQueueGetDevice(Queue);
    DeviceContext = DeviceGetContext(Device);
    Context = DriverGetContext(WdfDeviceGetDriver(Device));

    status = WdfRequestRetrieveInputBuffer(Request, Length, &InBuf, NULL);
    if (!NT_SUCCESS(status))  {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Failed to retrieve input buffer: %!STATUS!", 
            status);
        WdfRequestComplete(Request, status);
        return;
    }

    if (VioAudioWillBlockWrite(DeviceContext)) {
        WdfRequestComplete(Request, STATUS_CANT_WAIT);
        return;
    }

    buffer = ExAllocatePoolWithTag(NonPagedPool, Length,
        VIOAUDIO_MGMT_POOL_TAG);
    if (buffer == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Failed to allocate buffer.\n");
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    status = WdfMemoryCreateFromLookaside(Context->WriteBufferLookaside, &EntryHandle);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Failed to allocate write buffer entry: %!STATUS!",
            status);
        ExFreePoolWithTag(buffer, VIOAUDIO_MGMT_POOL_TAG);
        WdfRequestComplete(Request, STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    status = WdfRequestMarkCancelableEx(Request,
        VioAudioWriteRequestCancel);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE, "Failed to mark request as cancelable: %!STATUS!",
            status);
        ExFreePoolWithTag(buffer, VIOAUDIO_MGMT_POOL_TAG);
        WdfObjectDelete(EntryHandle);
        WdfRequestComplete(Request, status);
        return;
    }

    RtlCopyMemory(buffer, InBuf, Length);
    WdfRequestSetInformation(Request, (ULONG_PTR)Length);

    entry = (PWRITE_BUFFER_ENTRY)WdfMemoryGetBuffer(EntryHandle, NULL);
    entry->EntryHandle = EntryHandle;
    entry->Buffer = buffer;
    entry->Request = Request;

    if (VioAudioSendWriteBufferEntry(DeviceContext, entry, Length) <= 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_QUEUE,
            "Failed to send user's buffer.\n");

        ExFreePoolWithTag(buffer, VIOAUDIO_MGMT_POOL_TAG);
        WdfObjectDelete(EntryHandle);

        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED) {
            BOOLEAN Removed = FALSE;
            WdfRequestComplete(Request, Removed ?
                STATUS_INVALID_DEVICE_STATE : STATUS_INSUFFICIENT_RESOURCES);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");
}


VOID VioAudioEvtWriteIoStop(IN WDFQUEUE Queue,
    IN WDFREQUEST Request,
    IN ULONG ActionFlags)
{
    WDFDEVICE Device;
    PDEVICE_CONTEXT DeviceContext;

    Device = WdfIoQueueGetDevice(Queue);
    DeviceContext = DeviceGetContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Entry");

    if (ActionFlags & WdfRequestStopActionSuspend) {
        WdfRequestStopAcknowledge(Request, FALSE);
    } else if (ActionFlags & WdfRequestStopActionPurge) {
        if (WdfRequestUnmarkCancelable(Request) != STATUS_CANCELLED) {
            PSINGLE_LIST_ENTRY iter = &DeviceContext->WriteBuffersList;

            while ((iter = iter->Next) != NULL) {
                PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter, WRITE_BUFFER_ENTRY, ListEntry);
                if (entry->Request == Request) {
                    entry->Request = NULL;
                    break;
                }
            }

            WdfRequestComplete(Request, STATUS_OBJECT_NO_LONGER_EXISTS);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_QUEUE, "%!FUNC! Exit");
}
