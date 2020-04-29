#include <ntddk.h>
#include <wdm.h>
#include <ntstrsafe.h>

#include <portcls.h>
#include <ksdebug.h>

typedef struct Global_ {
    PDRIVER_OBJECT          driverObject;
    PDEVICE_OBJECT          ctrlDevObj;

    PFILE_OBJECT            fileObj;
    PDEVICE_OBJECT          pdoDevice;
    PDEVICE_OBJECT          nextDevice;
}Global, *PGlobal;

static Global global;

#ifdef DBG
#define LOG(fmt, ...)   DbgPrint("[SoundCardHook] "__FUNCTION__ "(%d): " fmt, __LINE__, ##__VA_ARGS__)
#else
#define LOG(...)   
#endif

#define TRY_UNREFERENCED_PARAMETER(...) __VA_ARGS__; 

#define SOUND_CARD_HOOK_POOLTAG               'KHCS'

#define DEV_NAME L"\\Device\\SoundCardFilterCtrlDevice"
#define DOS_NAME L"\\DosDevices\\SoundCardFilterCtrlDevice"

typedef enum FILTER_STATUS
{
    FIL_SUCCESS = 0,
    FIL_DENY,
    FIL_PASS,
}FILTER_STATUS;

/*
.rdata:00000001C000CC60 ; GUID stru_1C000CC60
.rdata:00000001C000CC60 stru_1C000CC60  dd 0A17579F0h           ; Data1
.rdata:00000001C000CC60                                         ; DATA XREF: sub_1C00493E0+14Co
.rdata:00000001C000CC60                 dw 4FECh                ; Data2
.rdata:00000001C000CC60                 dw 4936h                ; Data3
.rdata:00000001C000CC60                 db 93h, 64h, 24h, 94h, 60h, 86h, 3Bh, 0E5h; Data4
.rdata:00000001C000CC70 ; GUID InterfaceClassGuid
.rdata:00000001C000CC70 InterfaceClassGuid dd 86841137h            ; Data1
.rdata:00000001C000CC70                                         ; DATA XREF: sub_1C00493E0+FFo
.rdata:00000001C000CC70                 dw 0ED8Eh               ; Data2
.rdata:00000001C000CC70                 dw 4D97h                ; Data3
.rdata:00000001C000CC70                 db 99h, 75h, 0F2h, 0EDh, 56h, 0B4h, 43h, 0Eh; Data4
*/

#include <initguid.h>
DEFINE_GUID(GUID_DEVCLASS_MEDIA, 0x4d36e96cL, 0xe325, 0x11ce, 0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18);
DEFINE_GUID(GUID_MS_OFFICAIL_HDA, 0x86841137L, 0xED8E, 0x4D97, 0x99, 0x75, 0xF2, 0xED, 0x56, 0xB4, 0x43, 0x0E);
DEFINE_GUID(GUID_MS_OFFICAIL_HDA2, 0x0A17579F0L, 0x4FEC, 0x4936, 0x93, 0x64, 0x24, 0x94, 0x60, 0x86, 0x3B, 0xE5);
DEFINE_GUID(GUID_MS_OFFICAIL_AC97_ADAPTER_COMMON, 0x77481fa0, 0x1ef2, 0x11d2, 0x88, 0x3a, 0x0, 0x80, 0xc7, 0x65, 0x64, 0x7d);
DEFINE_GUID(GUID_MS_OFFICAIL_AC97_MINIPORT_TOPOLOGY, 0x245ae964, 0x49c8, 0x11d2, 0x95, 0xd7, 0x0, 0xc0, 0x4f, 0xb9, 0x25, 0xd3);

static CONST GUID *filterGuidArray[] = {
    &GUID_MS_OFFICAIL_HDA,
    &GUID_MS_OFFICAIL_HDA2,
    &GUID_MS_OFFICAIL_AC97_ADAPTER_COMMON,
    &GUID_MS_OFFICAIL_AC97_MINIPORT_TOPOLOGY,
};

static inline NTSTATUS IrpSkip(PIRP irp)
{
    IoSkipCurrentIrpStackLocation(irp);

    return IoCallDriver(global.nextDevice, irp);
}

static NTSTATUS DeviceIoControlBlocked(
    _In_  ULONG IoControlCode, _In_  PDEVICE_OBJECT DeviceObject, 
    _In_opt_  PVOID InputBuffer, _In_  ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer, _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl)
{
    KEVENT          event;
    IO_STATUS_BLOCK ioStatus;
    NTSTATUS        ntStatus;
    PIRP            irp;

    KeInitializeEvent(&event, NotificationEvent, FALSE);

    irp = IoBuildDeviceIoControlRequest(
        IoControlCode,
        DeviceObject,
        InputBuffer,
        InputBufferLength,
        OutputBuffer,
        OutputBufferLength,
        InternalDeviceIoControl, // IRP_MJ_INTERNAL_DEVICE_CONTROL
        &event,
        &ioStatus
    );

    if (!irp) {
        LOG("IoBuildDeviceIoControlRequest return NULL.\n");
        return STATUS_NO_MEMORY;
    }

    ntStatus = IoCallDriver(DeviceObject, irp);
    if (ntStatus == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        ntStatus = ioStatus.Status;
    }

    return ntStatus;
}

static NTSTATUS DeviceIoControlToBlocked(
    _In_  ULONG IoControlCode, _In_  PDEVICE_OBJECT DeviceObject,
    _In_opt_  PVOID InputBuffer, _In_  ULONG InputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl)
{
    return DeviceIoControlBlocked(IoControlCode, DeviceObject, 
        InputBuffer, InputBufferLength, 
        NULL, 0, 
        InternalDeviceIoControl);
}

static NTSTATUS DeviceIoControlFromBlocked(
    _In_  ULONG IoControlCode, _In_  PDEVICE_OBJECT DeviceObject,
    _Out_opt_ PVOID OutputBuffer, _In_ ULONG OutputBufferLength,
    _In_ BOOLEAN InternalDeviceIoControl)
{
    return DeviceIoControlBlocked(IoControlCode, DeviceObject, 
        NULL, 0,
        OutputBuffer, OutputBufferLength, 
        InternalDeviceIoControl);
}

static NTSTATUS TryHandleKsProp(PDEVICE_OBJECT DevObj, PIRP Irp, PIO_STACK_LOCATION IrpStack)
{
    NTSTATUS    ntStatus = STATUS_SUCCESS;
    PKSPROPERTY prop;

    TRY_UNREFERENCED_PARAMETER(DevObj, Irp, IrpStack);
    
    prop = (PKSPROPERTY)IrpStack->Parameters.DeviceIoControl.Type3InputBuffer;

#define TO_STRING(info) #info

    static const struct debug_info {
        ULONG flags;
        const char *info;
    } debug_infos[] = {
        {KSPROPERTY_TYPE_SET, "KSPROPERTY_TYPE_SET"},
        {KSPROPERTY_TYPE_GET, "KSPROPERTY_TYPE_GET"}
    };

    if (prop) {
        for (int i = 0; i < ARRAYSIZE(debug_infos); ++i) {
            const struct debug_info *info = &debug_infos[i];

            if (info->flags == prop->Flags) {
                LOG("prop->Flags %s(%d)\n", info->info, info->flags);
                break;
            }
        }
    }

    return ntStatus;
}

static FILTER_STATUS SoundCardFilterProcessStream(PDEVICE_OBJECT DevObj, PIRP Irp, PIO_STACK_LOCATION IrpStack, NTSTATUS *NtStatus)
{
    FILTER_STATUS fltStatus = FIL_PASS;

    TRY_UNREFERENCED_PARAMETER(DevObj, Irp, IrpStack);

    *NtStatus = STATUS_SUCCESS;

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_KS_WRITE_STREAM: // 2F8013h
        LOG("IOCTL_KS_WRITE_STREAM\n");

        break;    
    case IOCTL_KS_READ_STREAM: // 2F4017h
        LOG("IOCTL_KS_READ_STREAM\n");
        break;
    case IOCTL_KS_PROPERTY:
        *NtStatus = TryHandleKsProp(DevObj, Irp, IrpStack);
        LOG("IOCTL_KS_PROPERTY\n");
        break;
    }

    return fltStatus;
}

static NTSTATUS SoundCardFilterCommonDispatch(PDEVICE_OBJECT DevObj, PIRP Irp)
{
    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS           ntStatus = STATUS_SUCCESS;
    FILTER_STATUS      fltStatus = FIL_PASS;

    switch (irpStack->MajorFunction)
    {
    case IRP_MJ_DEVICE_CONTROL:
    case IRP_MJ_INTERNAL_DEVICE_CONTROL:
        fltStatus = SoundCardFilterProcessStream(DevObj, Irp, irpStack, &ntStatus);

        break;

    default:
        break;
    }

    return (fltStatus == FIL_PASS) ? IrpSkip(Irp) : ntStatus;
}

static NTSTATUS create_ctrl_device()
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    PDEVICE_OBJECT devObj;
    UNICODE_STRING devName;
    UNICODE_STRING dosName;

    RtlInitUnicodeString(&devName, DEV_NAME);
    RtlInitUnicodeString(&dosName, DOS_NAME);

    ntStatus = IoCreateDevice(
        global.driverObject,
        0,
        &devName, //dev name
        FILE_DEVICE_SOUND, // FILE_DEVICE_KS
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &devObj);
    if (!NT_SUCCESS(ntStatus)) {
        LOG("IoCreateDevice failed with 0x%X\n", ntStatus);
        return ntStatus;
    }

    ntStatus = IoCreateSymbolicLink(&dosName, &devName);
    if (!NT_SUCCESS(ntStatus)) {
        LOG("IoCreateSymbolicLink failed with 0x%X\n", ntStatus);
        IoDeleteDevice(devObj);
        return ntStatus;
    }

    // attach
    global.nextDevice = IoAttachDeviceToDeviceStack(devObj, global.pdoDevice);
    if (!global.nextDevice) {
        LOG("IoAttachDeviceToDeviceStack error.\n");
        IoDeleteDevice(devObj);
        IoDeleteSymbolicLink(&dosName);
        return STATUS_NOT_FOUND;
    }

    devObj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO | DO_DIRECT_IO;
    
    global.ctrlDevObj = devObj;

    return ntStatus;
}

NTSTATUS GetDeviceObjectByGuid(CONST GUID *DeviceGuid, PDEVICE_OBJECT *DevObj, PFILE_OBJECT *FileObj)
{
    NTSTATUS        ntStatus = STATUS_SUCCESS;
    PFILE_OBJECT    fileObj = NULL;
    PDEVICE_OBJECT  devObj = NULL;
    PWSTR           symbolicLinkList = NULL;
    UNICODE_STRING  objName;
    BOOL            isFound = FALSE;

    ntStatus = IoGetDeviceInterfaces(
        DeviceGuid,
        NULL,
        0,
        &symbolicLinkList
    );

    if (!NT_SUCCESS(ntStatus)){
        LOG("IoGetDeviceInterfaces failed with 0x%X\n", ntStatus);
        return ntStatus;
    }

    if (!symbolicLinkList) {
        LOG("IoGetDeviceInterfaces failed with no symbolic link list\n");
        return STATUS_UNSUCCESSFUL;
    }

    for (PWSTR symbolicLink = symbolicLinkList;
        symbolicLink[0] != L'\0' && symbolicLink[1] != L'\0';
        symbolicLink += wcslen(symbolicLink) + 1) {

        RtlInitUnicodeString(&objName, symbolicLink);

        LOG("Try Get object name: %wZ\n", &objName);

        ntStatus = IoGetDeviceObjectPointer(
            &objName,
            FILE_ALL_ACCESS,
            &fileObj,
            &devObj
        );

        if (NT_SUCCESS(ntStatus)) {

            ntStatus = ObReferenceObjectByPointer(devObj, FILE_ALL_ACCESS, NULL, KernelMode);
            if (!NT_SUCCESS(ntStatus)) {
                LOG("ObReferenceObjectByPointer failed with ntStatus 0x%08x\n", ntStatus);
                continue;
            }

            if (DevObj) {
                *DevObj = devObj;
            }

            if (FileObj) {
                *FileObj = fileObj;
            }

            isFound = TRUE;

            break;
        }
        else {
            LOG("IoGetDeviceObjectPointer Failed with ntStatus 0x%08x\n", ntStatus);
        }
    }

    ExFreePool(symbolicLinkList);

    if (!isFound) {
        ntStatus = STATUS_OBJECT_PATH_NOT_FOUND;
    }

    LOG("final status 0x%X, found %d, device %p, file object %p\n", ntStatus, isFound, devObj, fileObj);

    return ntStatus;
}

NTSTATUS create_sound_card_filter_ctrl_device(PDRIVER_OBJECT drvObj)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;

    UNICODE_STRING drvPath;
    UNICODE_STRING drvName;

    RtlInitUnicodeString(&drvPath, L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\SERVICES\\HdAudAddService");
    // RtlInitUnicodeString(&drvName, L"\\Device\\HdAudAddService");
    RtlInitUnicodeString(&drvName, L"\\Device\\0000005a"); // \\Device\\00000052
    
    RtlZeroMemory(&global, sizeof(global));

    global.driverObject = drvObj;

    ntStatus = ZwLoadDriver(&drvPath);
    if (!NT_SUCCESS(ntStatus)) {
        if (ntStatus != STATUS_IMAGE_ALREADY_LOADED) {
            LOG("ZwLoadDriver failed with ntStatus 0x%X\n", ntStatus);
            return ntStatus;
        }
    }

    ntStatus = GetDeviceObjectByGuid(&GUID_MS_OFFICAIL_HDA, &global.pdoDevice, &global.fileObj);
    if (!NT_SUCCESS(ntStatus)) {
        LOG("GetDeviceObjectByGuid failed with ntStatus 0x%X\n", ntStatus);
    
        ntStatus = IoGetDeviceObjectPointer(&drvName, FILE_ALL_ACCESS, &global.fileObj, &global.pdoDevice);
        if (!NT_SUCCESS(ntStatus)) {
            LOG("IoGetDeviceObjectPointer failed to get pdo with ntStatus 0x%X\n", ntStatus);
            return ntStatus;
        }
    }

    if (!NT_SUCCESS(ntStatus)) {
        LOG("Faied to retrive device object with ntStatus 0x%X\n", ntStatus);
        return ntStatus;
    }

    ///create filter device
    ntStatus = create_ctrl_device();
    if (!NT_SUCCESS(ntStatus)) {
        ObDereferenceObject(global.fileObj);
        return ntStatus;
    }

    return ntStatus;
}


/*
================================== Specifying Driver Load Order ==================================

HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\ServiceGroupOrder\List

System Reserved
EMS
WdfLoadGroup
Boot Bus Extender
System Bus Extender
SCSI miniport
Port
Primary Disk
SCSI Class
SCSI CDROM Class
FSFilter Infrastructure
FSFilter System
FSFilter Bottom
FSFilter Copy Protection
FSFilter Security Enhancer
FSFilter Open File
FSFilter Physical Quota Management
FSFilter Virtualization
FSFilter Encryption
FSFilter Compression
FSFilter Imaging
FSFilter HSM
FSFilter Cluster File System
FSFilter System Recovery
FSFilter Quota Management
FSFilter Content Screener
FSFilter Continuous Backup
FSFilter Replication
FSFilter Anti-Virus
FSFilter Undelete
FSFilter Activity Monitor
FSFilter Top
Filter
Boot File System
Base
Pointer Port
Keyboard Port
Pointer Class
Keyboard Class
Video Init
Video
Video Save
File System
Streams Drivers
NDIS Wrapper
COM Infrastructure
Event Log
PerceptionGroup
ProfSvc_Group
AudioGroup
UIGroup
MS_WindowsLocalValidation
PlugPlay
Cryptography
PNP_TDI
NDIS
TDI
iSCSI
NetBIOSGroup
ShellSvcGroup
SchedulerGroup
SpoolerGroup
SmartCardGroup
NetworkProvider
MS_WindowsRemoteValidation
NetDDEGroup
Parallel arbitrator
Extended Base
PCI Configuration
MS Transactions

HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\ServiceGroupOrder\List
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\HDAudBus\Start                          SERVICE_BOOT_START (0x0)
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\HDAudBus\Tag
HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Services\HDAudBus\Group                          Video Init / AudioGroup

IntcAzAudAddService / HDAudBus / HdAudAddService

https://docs.microsoft.com/zh-cn/windows-hardware/drivers/install/specifying-driver-load-order
*/

NTSTATUS DriverEntry(
    IN PDRIVER_OBJECT  DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;

    TRY_UNREFERENCED_PARAMETER(RegistryPath);

    for (UCHAR i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
        DriverObject->MajorFunction[i] = SoundCardFilterCommonDispatch;
    }

    ntStatus = create_sound_card_filter_ctrl_device(DriverObject);

    DriverObject->DriverUnload = NULL;

    return ntStatus;
}