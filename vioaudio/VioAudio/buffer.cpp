#include "precomp.h"

#if defined(EVENT_TRACING)
#include "buffer.tmh"
#endif

EXTERN_C_START

// Number of descriptors that queue contains.
#define QUEUE_DESCRIPTORS 128


NTSTATUS
VioAudioSendMsgBlock(
    IN struct virtqueue *vq,
    IN PVOID buf,
    IN UINT length)
{
    struct VirtIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s vq = %p\n", __FUNCTION__, vq);

    sg.physAddr = MmGetPhysicalAddress(buf);
    sg.length = length;

    if (0 <= virtqueue_add_buf(vq, &sg, 1, 0, buf, NULL, 0)) {
        virtqueue_kick(vq);

        while (!virtqueue_get_buf(vq, &length)) {
            LARGE_INTEGER interval;
            interval.QuadPart = -1;
            KeDelayExecutionThread(KernelMode, FALSE, &interval);
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return STATUS_SUCCESS;
}

PVBUFFER
VioAudioAllocateBuffer(
    IN size_t buf_size
)
{
    PVBUFFER buf;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    buf = (PVBUFFER)ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(VBUFFER),
        VIOAUDIO_MGMT_POOL_TAG
    );
    
    if (buf == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        return NULL;
    }
    
    buf->va_buf = ExAllocatePoolWithTag(
        NonPagedPool,
        buf_size,
        VIOAUDIO_MGMT_POOL_TAG
    );
    
    if (buf->va_buf == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "ExAllocatePoolWithTag failed, %s::%d\n", __FUNCTION__, __LINE__);
        ExFreePoolWithTag(buf, VIOAUDIO_MGMT_POOL_TAG);
        return NULL;
    }

    buf->pa_buf = MmGetPhysicalAddress(buf->va_buf);
    buf->len = 0;
    buf->offset = 0;
    buf->size = buf_size;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    
    return buf;
}

VOID
VioAudioFreeBuffer(
    IN PVBUFFER buf
)
{
    ASSERT(buf);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p, buf->va_buf = %p\n", __FUNCTION__, buf, buf->va_buf);
    if (buf->va_buf) {
        ExFreePoolWithTag(buf->va_buf, VIOAUDIO_MGMT_POOL_TAG);
        buf->va_buf = NULL;
    }

    ExFreePoolWithTag(buf, VIOAUDIO_MGMT_POOL_TAG);
    
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VioAudioSendVBuf(
    IN struct virtqueue *vq,
    IN PVBUFFER buf)
{
    NTSTATUS  status = STATUS_SUCCESS;
    struct VirtIOBufferDescriptor sg;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s  buf = %p\n", __FUNCTION__, buf);

    if (buf == NULL) {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    if (vq == NULL) {
        ASSERT(0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    sg.physAddr = buf->pa_buf;
    sg.length = buf->size;

    if (0 > virtqueue_add_buf(vq, &sg, 0, 1, buf, NULL, 0)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "<-- %s cannot add_buf\n", __FUNCTION__);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    virtqueue_kick(vq);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
    
    return status;
}


PVOID
VioAudioGetVBuf(
    IN struct virtqueue *vq
)
{
    PVBUFFER buf = NULL;
    UINT len;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (vq) {
        buf = (PVBUFFER)virtqueue_get_buf(vq, &len);
        if (buf) {
            buf->len = len;
            buf->offset = 0;
        }
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);

    return buf;
}

SSIZE_T
VioAudioFillReadBufLocked(
    IN PDEVICE_CONTEXT DeviceContext,
    IN PVOID outbuf,
    IN SIZE_T count
)
{
    PVBUFFER buf;
    NTSTATUS  status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    if (!count || !VioAudioPortHasDataLocked(DeviceContext))
        return 0;

    buf = DeviceContext->RxBuffer;
    count = min(count, buf->len - buf->offset);

    RtlCopyMemory(outbuf, (PVOID)((LONG_PTR)buf->va_buf + buf->offset), count);

    buf->offset += count;

    if (buf->offset == buf->len) {
        DeviceContext->RxBuffer = NULL;

        status = VioAudioSendVBuf(DeviceContext->RxVirtQueue, buf);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING, "%s::%d  VioAudioSendVBuf failed\n", __FUNCTION__, __LINE__);
        }
    }
    
    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);

    return count;
}

size_t VioAudioSendWriteBufferEntry(
    IN PDEVICE_CONTEXT DeviceContext,
    IN PWRITE_BUFFER_ENTRY Entry,
    IN size_t Length
)
{
    struct VirtIOBufferDescriptor sg[QUEUE_DESCRIPTORS];
    struct virtqueue *vq = DeviceContext->TxVirtQueue;
    PVOID buffer = Entry->Buffer;
    size_t length = Length;
    int out = 0, prepared = 0;
    int ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING,
        "--> %s Buffer: %p Length: %d\n", __FUNCTION__, Entry->Buffer, Length);

    if (BYTES_TO_PAGES(Length) > QUEUE_DESCRIPTORS) {
        return 0;
    }

    while (length > 0) {
        sg[out].physAddr = MmGetPhysicalAddress(buffer);
        sg[out].length = min(length, PAGE_SIZE);

        buffer = (PVOID)((LONG_PTR)buffer + sg[out].length);
        length -= sg[out].length;
        out += 1;
    }

    WdfSpinLockAcquire(DeviceContext->TxQueueLock);

    ret = virtqueue_add_buf(vq, sg, out, 0, Entry->Buffer, NULL, 0);
    if (ret >= 0) {
        prepared = virtqueue_kick_prepare(vq);
        PushEntryList(&DeviceContext->WriteBuffersList, &Entry->ListEntry);
    } else {
        DeviceContext->TxVqFull = TRUE;
        Length = 0;
        TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING,
            "Error adding buffer to queue (ret = %d)\n", ret);
    }

    WdfSpinLockRelease(DeviceContext->TxQueueLock);

    if (prepared) {
        // notify can run without the lock held
        virtqueue_notify(vq);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);

    return Length;
}


VOID 
VioAudioProcessInputBuffers(
    IN PDEVICE_CONTEXT DeviceContext
)
{
    NTSTATUS status;
    ULONG Read = 0;
    WDFREQUEST Request = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(DeviceContext->RxQueueLock);
    if (!DeviceContext->RxBuffer) {
        DeviceContext->RxBuffer = (PVBUFFER)VioAudioGetVBuf(DeviceContext->RxVirtQueue);
    }

    if (!DeviceContext->GuestConnected) {
        VioAudioDiscardDataLocked(DeviceContext);
    }

    if (DeviceContext->RxBuffer && DeviceContext->PendingReadRequest) {
        status = WdfRequestUnmarkCancelable(DeviceContext->PendingReadRequest);

        if (status != STATUS_CANCELLED) {
            PVOID Buffer;
            size_t Length;

            status = WdfRequestRetrieveOutputBuffer(DeviceContext->PendingReadRequest, 0, &Buffer, &Length);
            if (NT_SUCCESS(status)) {
                Request = DeviceContext->PendingReadRequest;
                DeviceContext->PendingReadRequest = NULL;

                Read = (ULONG)VioAudioFillReadBufLocked(DeviceContext, Buffer, Length);
            } else {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_QUEUEING,
                    "Failed to retrieve output buffer (Status: %x Request: %p).\n",
                    status, Request);
            }
        } else {
            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING,
                "Request %p was cancelled.\n", Request);
        }
    }
    WdfSpinLockRelease(DeviceContext->RxQueueLock);

    if (Request != NULL) {
        // no need to have the lock when completing the request
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, Read);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s\n", __FUNCTION__);
}

BOOLEAN 
VioAudioReclaimConsumedBuffers(
    IN PDEVICE_CONTEXT DeviceContext
)
{
    WDFREQUEST request;
    SINGLE_LIST_ENTRY ReclaimedList = { NULL };
    PSINGLE_LIST_ENTRY iter, last = &ReclaimedList;
    PVOID buffer;
    UINT len;
    struct virtqueue *vq = DeviceContext->TxVirtQueue;
    BOOLEAN ret;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "--> %s\n", __FUNCTION__);

    WdfSpinLockAcquire(DeviceContext->TxQueueLock);

    if (vq) {
        while ((buffer = virtqueue_get_buf(vq, &len)) != NULL) {
            iter = &DeviceContext->WriteBuffersList;

            while (iter->Next != NULL) {
                PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter->Next,
                    WRITE_BUFFER_ENTRY, ListEntry);

                if (buffer == entry->Buffer) {
                    if (entry->Request != NULL) {
                        request = entry->Request;

                        if (WdfRequestUnmarkCancelable(request) == STATUS_CANCELLED) {
                            TraceEvents(TRACE_LEVEL_INFORMATION, DBG_QUEUEING,
                                "Request %p was cancelled.\n", request);
                            entry->Request = NULL;
                        }
                    }

                    // remove from WriteBuffersList
                    iter->Next = entry->ListEntry.Next;

                    // append to ReclaimedList
                    last->Next = &entry->ListEntry;
                    last = last->Next;
                    last->Next = NULL;

                } else {
                    iter = iter->Next;
                }
            };

            DeviceContext->TxVqFull = FALSE;
        }
    }

    ret = DeviceContext->TxVqFull;

    WdfSpinLockRelease(DeviceContext->TxQueueLock);

    // no need to hold the lock to complete requests and free buffers
    while ((iter = PopEntryList(&ReclaimedList)) != NULL) {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter,
            WRITE_BUFFER_ENTRY, ListEntry);

        request = entry->Request;
        if (request != NULL) {
            WdfRequestCompleteWithInformation(request, STATUS_SUCCESS,
                WdfRequestGetInformation(request));
        }

        ExFreePoolWithTag(entry->Buffer, VIOAUDIO_MGMT_POOL_TAG);
        WdfObjectDelete(entry->EntryHandle);
    };

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_QUEUEING, "<-- %s Full: %d\n",
        __FUNCTION__, ret);

    return ret;
}


EXTERN_C_END