/*++

Module Name:

    device.c - Device handling events for example driver.

Abstract:

   This file contains the device entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"
#include "precomp.h"

#if defined(EVENT_TRACING)
#include "device.tmh"
#endif

EXTERN_C_START

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, VioAudioCreateDevice)
#endif


VOID
VioAudioRoutine(
    IN PVOID pContext
)
{
    WDFOBJECT Device = (WDFOBJECT)pContext;
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);

    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "VioAudio thread started....\n");

    for ( ; ; ) {
        status = KeWaitForSingleObject(&devCtx->WakeUpThread, Executive,
            KernelMode, FALSE, NULL);
        
        if (STATUS_WAIT_0 == status) {

            if (devCtx->bShutDown) {
                TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Exiting Thread!\n");
                break;
            }
            else {

            }
        }
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "Thread about to exit...\n");

    PsTerminateSystemThread(STATUS_SUCCESS);
}


NTSTATUS
VioAudioCreateWorkerThread(
    IN WDFDEVICE  Device
)
{
    PDEVICE_CONTEXT     devCtx = DeviceGetContext(Device);
    NTSTATUS            status = STATUS_SUCCESS;
    HANDLE              hThread = 0;
    OBJECT_ATTRIBUTES   oa;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);
    devCtx->bShutDown = FALSE;

    if (NULL == devCtx->Thread) {
        InitializeObjectAttributes(&oa, NULL,
            OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
            VioAudioRoutine, Device);

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
                "failed to create worker thread status 0x%08x\n", status);
            return status;
        }

        ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL,
            KernelMode, (PVOID*)&devCtx->Thread, NULL);
        KeSetPriorityThread(devCtx->Thread, LOW_REALTIME_PRIORITY);

        ZwClose(hThread);
    }

    KeSetEvent(&devCtx->WakeUpThread, EVENT_INCREMENT, FALSE);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<-- %s\n", __FUNCTION__);
    return status;
}


NTSTATUS
VioAudioEvtDevicePrepareHardware(
    IN WDFDEVICE    Device,
    IN WDFCMRESLIST ResourceList,
    IN WDFCMRESLIST ResourceListTranslated
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourceList);

    PAGED_CODE();

    devCtx = DeviceGetContext(Device);

    status = VirtIOWdfInitialize(
        &devCtx->VioDevice,
        Device,
        ResourceListTranslated,
        NULL,
        VIOAUDIO_MGMT_POOL_TAG);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP, "VirtIOWdfInitialize failed with %x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES  attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->CtrlInQueueLock
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfSpinLockCreate CtrlInQueueLock failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->CtrlOutQueueLock
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfSpinLockCreate CtrlOutQueueLock failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->RxQueueLock
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfSpinLockCreate RxQueueLock failed 0x%x\n", status);
        return status;
    }

    status = WdfSpinLockCreate(
        &attributes,
        &devCtx->TxQueueLock
    );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_INIT,
            "WdfSpinLockCreate RxQueueLock failed 0x%x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return status;
}

NTSTATUS
VioAudioEvtDeviceReleaseHardware(
    IN WDFDEVICE      Device,
    IN WDFCMRESLIST   ResourcesTranslated
)
{
    PDEVICE_CONTEXT     devCtx = NULL;

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(ResourcesTranslated);

    PAGED_CODE();

    devCtx = DeviceGetContext(Device);

    VirtIOWdfShutdown(&devCtx->VioDevice);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);
    return STATUS_SUCCESS;
}


NTSTATUS
VioAudioEvtDeviceD0Entry(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE PreviousState
)
{
    NTSTATUS        status = STATUS_SUCCESS;
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);

    UNREFERENCED_PARAMETER(PreviousState);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    status = VioAudioInit(Device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "VioAudioInit failed with status 0x%08x\n", status);
        VioAudioTerm(Device);
        return status;
    }

    VioAudioSendCtrlEvent(Device, VIRTIO_AUDIO_DEVICE_READY, 1);

    if (devCtx->GuestConnected) {
        VioAudioSendCtrlEvent(Device, VIRTIO_AUDIO_DEVICE_OPEN, 1);
    }

    status = VioAudioCreateWorkerThread(Device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "VioAudioCreateWorkerThread failed with status 0x%08x\n", status);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
VioAudioEvtDeviceD0Exit(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
)
{
    PDEVICE_CONTEXT devCtx = DeviceGetContext(Device);
    PSINGLE_LIST_ENTRY iter;

    UNREFERENCED_PARAMETER(TargetState);
    UNREFERENCED_PARAMETER(devCtx);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    DisableInterrupt(devCtx);

    if (devCtx->GuestConnected) {
        VioAudioSendCtrlEvent(Device, VIRTIO_AUDIO_DEVICE_OPEN, 0);
    }

    WdfSpinLockAcquire(devCtx->RxQueueLock);
    VioAudioDiscardDataLocked(devCtx);
    devCtx->RxBuffer = NULL;
    WdfSpinLockRelease(devCtx->RxQueueLock);

    VioAudioReclaimConsumedBuffers(devCtx);

    VioAudioDrainQueue(devCtx->RxVirtQueue);

    iter = PopEntryList(&devCtx->WriteBuffersList);
    while (iter != NULL) {
        PWRITE_BUFFER_ENTRY entry = CONTAINING_RECORD(iter,
            WRITE_BUFFER_ENTRY, ListEntry);

        ExFreePoolWithTag(entry->Buffer, VIOAUDIO_MGMT_POOL_TAG);
        WdfObjectDelete(entry->EntryHandle);

        iter = PopEntryList(&devCtx->WriteBuffersList);
    };

#if 0
    VioAudioTerm(Device);
#endif

    return STATUS_SUCCESS;
}


NTSTATUS
VioAudioEvtDeviceD0ExitPreInterruptsDisabled(
    IN  WDFDEVICE Device,
    IN  WDF_POWER_DEVICE_STATE TargetState
)
{
    PDEVICE_CONTEXT       devCtx = DeviceGetContext(Device);

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "<--> %s\n", __FUNCTION__);

    PAGED_CODE();

    UNREFERENCED_PARAMETER(TargetState);
    UNREFERENCED_PARAMETER(devCtx);

#if 0

    BalloonCloseWorkerThread(Device);
    if (TargetState == WdfPowerDeviceD3Final)
    {
        while (devCtx->num_pages)
        {
            BalloonLeak(Device, devCtx->num_pages);
        }

        BalloonSetSize(Device, devCtx->num_pages);
    }
#endif

    return STATUS_SUCCESS;
}


VOID
VioAudioEvtDeviceContextCleanup(
    IN WDFOBJECT  Device
)
{
    PDEVICE_CONTEXT     devCtx = DeviceGetContext((WDFDEVICE)Device);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_INIT, "--> %s\n", __FUNCTION__);

    UNREFERENCED_PARAMETER(devCtx);
}

static
NTSTATUS
VioAudioInitInterruptHandling(
    IN WDFDEVICE hDevice)
{
    WDF_OBJECT_ATTRIBUTES        attributes;
    WDF_INTERRUPT_CONFIG         interruptConfig;
    NTSTATUS                     status = STATUS_SUCCESS;

    PDEVICE_CONTEXT              deviceContext;

    TraceEvents(TRACE_LEVEL_VERBOSE, DBG_HW_ACCESS, "--> %s\n", __FUNCTION__);

    deviceContext = DeviceGetContext(hDevice);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
        VioAudioInterruptIsr,
        VioAudioInterruptDpc);

    interruptConfig.EvtInterruptEnable = VioAudioInterruptEnable;
    interruptConfig.EvtInterruptDisable = VioAudioInterruptDisable;

    status = WdfInterruptCreate(hDevice,
        &interruptConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &deviceContext->WdfInterrupt);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create control queue interrupt: %x\n", status);
        return status;
    }

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    WDF_INTERRUPT_CONFIG_INIT(&interruptConfig,
        VioAudioInterruptIsr, VioAudioQueuesInterruptDpc);

    status = WdfInterruptCreate(hDevice, &interruptConfig, &attributes,
        &deviceContext->QueuesInterrupt);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_HW_ACCESS,
            "Failed to create general queue interrupt: %x\n", status);
        return status;
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_HW_ACCESS, "<-- %s\n", __FUNCTION__);
    return status;
}

NTSTATUS
VioAudioCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
/*++

Routine Description:

    Worker routine called to create a device and its software resources.

Arguments:

    DeviceInit - Pointer to an opaque init structure. Memory for this
                    structure will be freed by the framework when the WdfDeviceCreate
                    succeeds. So don't access the structure after that point.

Return Value:

    NTSTATUS

--*/
{
    WDF_OBJECT_ATTRIBUTES        deviceAttributes;
    PDEVICE_CONTEXT              deviceContext;
    WDFDEVICE                    device;
    NTSTATUS                     status = STATUS_SUCCESS;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_FILEOBJECT_CONFIG        fileConfig;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "--> %s\n", __FUNCTION__);

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        VioAudioDeviceCreate,
        VioAudioDeviceClose,
        WDF_NO_EVENT_CALLBACK
    );

    WdfDeviceInitSetFileObjectConfig(
        DeviceInit,
        &fileConfig,
        WDF_NO_OBJECT_ATTRIBUTES
    );

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

    pnpPowerCallbacks.EvtDevicePrepareHardware = VioAudioEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VioAudioEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VioAudioEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VioAudioEvtDeviceD0Exit;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = VioAudioEvtDeviceD0ExitPreInterruptsDisabled;

    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = VioAudioEvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "WdfDeviceCreate failed with status 0x%08x\n", status);
        return status;
    }

    //
        // Get a pointer to the device context structure that we just associated
        // with the device object. We define this structure in the device.h
        // header file. DeviceGetContext is an inline function generated by
        // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
        // This function will do the type checking and return the device context.
        // If you pass a wrong object handle it will return NULL and assert if
        // run under framework verifier mode.
        //
    deviceContext = DeviceGetContext(device);

    //
    // Initialize the context.
    //

    status = VioAudioInitInterruptHandling(device);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "VioAudioInitInterruptHandling failed: 0x%08x\n", status);
        return status;
    }

    //
    // Create a device interface so that applications can find and talk
    // to us.
    //
    status = WdfDeviceCreateDeviceInterface(
        device,
        &GUID_DEVINTERFACE_VioAudio,
        NULL // ReferenceString
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, DBG_PNP,
            "WdfDeviceCreateDeviceInterface failed with status 0x%08x\n", status);
        return status;
    }

    //
    // Initialize the I/O Package and any Queues
    //
    status = VioAudioQueueInitialize(device);

    KeInitializeEvent(&deviceContext->WakeUpThread,
        SynchronizationEvent,
        FALSE
    );

    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_PNP, "<-- %s\n", __FUNCTION__);

    return status;
}

EXTERN_C_END