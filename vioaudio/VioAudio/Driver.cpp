/*++

Module Name:

    driver.c

Abstract:

    This file contains the driver entry points and callbacks.

Environment:

    Kernel-mode Driver Framework

--*/

#include "driver.h"

#if defined(EVENT_TRACING)
#include "driver.tmh"
#endif

#include "precomp.h"
#include "adapter.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (INIT, DriverEntry)
#pragma alloc_text (PAGE, VioAudioEvtDeviceAdd)
#pragma alloc_text (PAGE, VioAudioEvtDriverContextCleanup)
#endif

typedef void(*PcDriverUnloadPfn) (PDRIVER_OBJECT);
PcDriverUnloadPfn gPCDriverUnloadRoutine = NULL;

typedef NTSTATUS(*PnpHandlerPfn) (
    _In_ DEVICE_OBJECT *_DeviceObject,
    _Inout_ IRP *_Irp);
PnpHandlerPfn  gWdfDriverPnpHandler = NULL;
PnpHandlerPfn  gPCDriverPnpHandler = NULL;

NTSTATUS
CompletePnpHandler
(
    _In_ DEVICE_OBJECT *_DeviceObject,
    _Inout_ IRP *_Irp
)
/*++

Routine Description:

  Handles PnP IRPs

Arguments:

  _DeviceObject - Functional Device object pointer.

  _Irp - The Irp being passed

Return Value:

  NT status code.

--*/
{
    NTSTATUS status = STATUS_SUCCESS;

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (gPCDriverPnpHandler != NULL) {
        status = gPCDriverPnpHandler(_DeviceObject, _Irp);
    }

    if (!NT_SUCCESS(status) && gWdfDriverPnpHandler != NULL) {
        status = gWdfDriverPnpHandler(_DeviceObject, _Irp);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

extern "C"
void DriverUnload (
    _In_ PDRIVER_OBJECT DriverObject
)
/*++

Routine Description:

  Our driver unload routine. This just frees the WDF driver object.

Arguments:

  DriverObject - pointer to the driver object

Environment:

    PASSIVE_LEVEL

--*/
{
    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    if (DriverObject == NULL) {
        goto Done;
    }

    //
    // Invoke first the port unload.
    //
    if (gPCDriverUnloadRoutine != NULL) {
        gPCDriverUnloadRoutine(DriverObject);
    }

    //
    // Unload WDF driver object. 
    //
    if (WdfGetDriver() != NULL) {
        WdfDriverMiniportUnload(WdfGetDriver());
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

Done:
    return;
}

extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C"
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
/*++

Routine Description:
    DriverEntry initializes the driver and is the first routine called by the
    system after the driver is loaded. DriverEntry specifies the other entry
    points in the function driver, such as EvtDevice and DriverUnload.

Parameters Description:

    DriverObject - represents the instance of the function driver that is loaded
    into memory. DriverEntry must initialize members of DriverObject before it
    returns to the caller. DriverObject is allocated by the system before the
    driver is loaded, and it is released by the system after the system unloads
    the function driver from memory.

    RegistryPath - represents the driver specific path in the Registry.
    The function driver can use the path to store driver related data between
    reboots. The path does not store hardware instance specific data.

Return Value:

    STATUS_SUCCESS if successful,
    STATUS_UNSUCCESSFUL otherwise.

--*/
{
    WDF_DRIVER_CONFIG      config;
    NTSTATUS               status;
    WDFDRIVER              driver;
    WDF_OBJECT_ATTRIBUTES  attributes;
    PDRIVER_CONTEXT        context;

    //
    // Initialize WPP Tracing
    //
    WPP_INIT_TRACING(DriverObject, RegistryPath);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Register a cleanup callback so that we can call WPP_CLEANUP when
    // the framework driver object is deleted during driver unload.
    //
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DRIVER_CONTEXT);

    attributes.EvtCleanupCallback = VioAudioEvtDriverContextCleanup;
    config.DriverPoolTag = VIOAUDIO_MGMT_POOL_TAG;

    WDF_DRIVER_CONFIG_INIT(&config,
                           VioAudioEvtDeviceAdd
                           );

    config.EvtDriverUnload = VioAudioEvtWdfDriverUnload;

    // WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    ////
    //// Set WdfDriverInitNoDispatchOverride flag to tell the framework
    //// not to provide dispatch routines for the driver. In other words,
    //// the framework must not intercept IRPs that the I/O manager has
    //// directed to the driver. In this case, they will be handled by Audio
    //// port driver.
    ////
    // config.DriverInitFlags |= WdfDriverInitNoDispatchOverride;

    status = WdfDriverCreate(DriverObject,
                             RegistryPath,
                             &attributes,
                             &config,
                             &driver
                             );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfDriverCreate failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    context = DriverGetContext(driver);
    if (!context) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "DriverGetContext failed.");
        WPP_CLEANUP(DriverObject);
        return status;
    }

    // create a lookaside list used for allocating WRITE_BUFFER_ENTRY
    // structures by all devices
    status = WdfLookasideListCreate(WDF_NO_OBJECT_ATTRIBUTES,
        sizeof(WRITE_BUFFER_ENTRY),
        NonPagedPool,
        WDF_NO_OBJECT_ATTRIBUTES,
        VIOAUDIO_MGMT_POOL_TAG,
        &context->WriteBufferLookaside);
    if (!NT_SUCCESS(status)) {
        WPP_CLEANUP(DriverObject);
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "WdfLookasideListCreate failed %!STATUS!", status);
        return status;
    }


#if 0
    // gWdfDriverPnpHandler = DriverObject->MajorFunction[IRP_MJ_PNP];

    status = AudioDeviceDriverEntry(DriverObject,
                            RegistryPath
                            );
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DRIVER, "VadDriverEntry failed %!STATUS!", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }

    gPCDriverPnpHandler = DriverObject->MajorFunction[IRP_MJ_PNP];

    gPCDriverUnloadRoutine = DriverObject->DriverUnload;
    DriverObject->DriverUnload = DriverUnload;

    DriverObject->MajorFunction[IRP_MJ_PNP] = CompletePnpHandler;
#endif

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID 
VioAudioEvtWdfDriverUnload(
    WDFDRIVER Driver
)
{
    UNREFERENCED_PARAMETER(Driver);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");
}

NTSTATUS
VioAudioEvtDeviceAdd(
    _In_    WDFDRIVER       Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
    NTSTATUS status;

    UNREFERENCED_PARAMETER(Driver);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    status = VioAudioCreateDevice(DeviceInit);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Exit");

    return status;
}

VOID
VioAudioEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
/*++
Routine Description:

    Free all the resources allocated in DriverEntry.

Arguments:

    DriverObject - handle to a WDF Driver object.

Return Value:

    VOID.

--*/
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DRIVER, "%!FUNC! Entry");

    //
    // Stop WPP Tracing
    //
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
}
