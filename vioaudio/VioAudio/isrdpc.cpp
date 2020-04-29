/*++

Module Name:

    isrdpc.cpp

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"

#if defined(EVENT_TRACING)
#include "isrdpc.tmh"
#endif

#include "precomp.h"

#ifdef ALLOC_PRAGMA
#endif

BOOLEAN
VioAudioInterruptIsr(
    IN WDFINTERRUPT WdfInterrupt,
    IN ULONG        MessageID
)
{
    PDEVICE_CONTEXT     devCtx = NULL;
    WDFDEVICE           Device;

    UNREFERENCED_PARAMETER(MessageID);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INTERRUPT, "--> %s\n", __FUNCTION__);
    Device = WdfInterruptGetDevice(WdfInterrupt);
    devCtx = DeviceGetContext(Device);

    if (VirtIOWdfGetISRStatus(&devCtx->VioDevice) > 0) {
        WdfInterruptQueueDpcForIsr(WdfInterrupt);
        return TRUE;
    }

    return FALSE;
}

VOID
VioAudioInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
)
{
    unsigned int          len;
    PDEVICE_CONTEXT       devCtx = DeviceGetContext(WdfDevice);
    PVOID                 buffer;
    WDFDEVICE             Device = WdfInterruptGetDevice(WdfInterrupt);
    WDF_INTERRUPT_INFO    info;

    BOOLEAN               bHostAck = FALSE;

    UNREFERENCED_PARAMETER(WdfInterrupt);
    UNREFERENCED_PARAMETER(bHostAck);
    UNREFERENCED_PARAMETER(buffer);
    UNREFERENCED_PARAMETER(devCtx);
    UNREFERENCED_PARAMETER(len);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_DPC, "--> %s\n", __FUNCTION__);

    VioAudioCtrlWorkHandler(Device);

    WDF_INTERRUPT_INFO_INIT(&info);
    WdfInterruptGetInfo(devCtx->QueuesInterrupt, &info);

    // Using the queues' DPC if only one interrupt is available.
    if (info.Vector == 0) {
        VioAudioQueuesInterruptDpc(WdfInterrupt, WdfDevice);
    }

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

VOID
VioAudioQueuesInterruptDpc(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFOBJECT    WdfDevice
)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(WdfDevice);

    UNREFERENCED_PARAMETER(WdfInterrupt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "--> %s\n", __FUNCTION__);

    VioAudioProcessInputBuffers(devCtx);
    VioAudioReclaimConsumedBuffers(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_DPC, "<-- %s\n", __FUNCTION__);
}

NTSTATUS
VioAudioInterruptEnable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
)
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = DeviceGetContext(WdfDevice);
    EnableInterrupt(WdfInterrupt, devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}

NTSTATUS
VioAudioInterruptDisable(
    IN WDFINTERRUPT WdfInterrupt,
    IN WDFDEVICE    WdfDevice
)
{
    PDEVICE_CONTEXT     devCtx = NULL;
    UNREFERENCED_PARAMETER(WdfInterrupt);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "--> %s\n", __FUNCTION__);

    devCtx = DeviceGetContext(WdfDevice);
    DisableInterrupt(devCtx);

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}
