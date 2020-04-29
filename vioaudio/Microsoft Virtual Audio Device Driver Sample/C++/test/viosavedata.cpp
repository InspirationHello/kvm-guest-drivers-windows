#pragma warning (disable : 4127)
#pragma warning (disable : 26165)

#include <ntifs.h>

#include <msvad.h>
#include "savedata.h"
#include <ntstrsafe.h>   // This is for using RtlStringcbPrintf

#include "viosavedata.h"

#include <initguid.h>

DEFINE_GUID(GUID_DEVINTERFACE_VioAudio,
    0xc45687db, 0xd100, 0x4da4, 0xb7, 0x60, 0x31, 0x59, 0x2e, 0x61, 0x06, 0xaa);
// {c45687db-d100-4da4-b760-31592e6106aa}


CVioSaveData::CVioSaveData() :
    m_DevObj(NULL)
{
}

CVioSaveData::~CVioSaveData()
{
    if (m_FileHandle) {
        // FileClose();
    }

    if (m_DevObj) {
        ObDereferenceObject(m_DevObj);
    }
}

NTSTATUS CVioSaveData::Initialize(ULONG StreamId)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    PWSTR    symbolicLinkList;

    UNREFERENCED_PARAMETER(StreamId);

    ntStatus = IoGetDeviceInterfaces(
        &GUID_DEVINTERFACE_VioAudio,
        NULL,
        0,
        &symbolicLinkList
    );

    if (NT_SUCCESS(ntStatus) && NULL != symbolicLinkList) {
        PFILE_OBJECT   fileObj;
        PDEVICE_OBJECT devObj;
        UNICODE_STRING objName;

        IO_STATUS_BLOCK ioStatus = { 0 };

        for (PWSTR symbolicLink = symbolicLinkList;
            symbolicLink[0] != NULL && symbolicLink[1] != NULL;
            symbolicLink += wcslen(symbolicLink) + 1) {

            RtlInitUnicodeString(&objName, symbolicLink);

            DPF_ENTER(("VioAudioSaveData: Get object name: %wZ\n", &objName));

            ntStatus = IoGetDeviceObjectPointer(
                &objName,
                FILE_ALL_ACCESS,
                &fileObj,
                &devObj
            );

            if (NT_SUCCESS(ntStatus)) {
                
                ntStatus = ObReferenceObjectByPointer(devObj, FILE_ALL_ACCESS, NULL, KernelMode);
                if (!NT_SUCCESS(ntStatus)) {
                    DPF_ENTER(("VioAudioSaveData: ObReferenceObjectByPointer Successed with status 0x%08x\n", ntStatus));
                    continue;
                }

                m_FileName.Length = 0;
                m_FileName.MaximumLength = (USHORT)((objName.Length * sizeof(WCHAR)) + sizeof(WCHAR));//convert to wchar and add room for NULL
                m_FileName.Buffer = (PWSTR)
                    ExAllocatePoolWithTag
                    (
                        PagedPool,
                        m_FileName.MaximumLength,
                        MSVAD_POOLTAG
                    );
                if (!m_FileName.Buffer) {
                    ObDereferenceObject(devObj);
                    ntStatus = STATUS_INSUFFICIENT_RESOURCES;

                    DPF(D_TERSE, ("[Could not allocate memory for FileName]"));

                    break;
                }

                RtlUnicodeStringCopy(&m_FileName, &objName);

                m_DevObj = devObj;

                DPF_ENTER(("VioAudioSaveData: IoGetDeviceObjectPointer Successed with status 0x%08x\n", ntStatus));
                break;
            }
            else {
                DPF_ENTER(("VioAudioSaveData: IoGetDeviceObjectPointer Failed with status 0x%08x\n", ntStatus));
            }
        }
    }

    ExFreePool(symbolicLinkList);

    if (NT_SUCCESS(ntStatus)) {
        // Create data file.
        InitializeObjectAttributes
        (
            &m_objectAttributes,
            &m_FileName,
            OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
            NULL,
            NULL
        );
    }

    DPF_ENTER(("VioAudioSaveData: FileOpen Final status 0x%08x\n", ntStatus));

    return ntStatus;
}

NTSTATUS CVioSaveData::SetDataFormat(IN  PWAVEFORMATEX pWaveFormat)
{
    return CSaveBackend::SetDataFormat(pWaveFormat);
}

NTSTATUS CVioSaveData::FileClose(void)
{
    return CSaveBackend::FileClose();
}

NTSTATUS CVioSaveData::FileOpen(IN BOOL fOverWrite)
{
#if 0
    PWSTR symbolicLinkList;
    NTSTATUS ntStatus;

    UNREFERENCED_PARAMETER(fOverWrite);

    ntStatus = IoGetDeviceInterfaces(
        &GUID_DEVINTERFACE_VioAudio,
        NULL,
        0,
        &symbolicLinkList
    );

    if (NT_SUCCESS(ntStatus) && NULL != symbolicLinkList) {
        PFILE_OBJECT   fileObj;
        PDEVICE_OBJECT devObj;
        UNICODE_STRING objName;

        IO_STATUS_BLOCK ioStatus = { 0 };

        for (PWSTR symbolicLink = symbolicLinkList;
            symbolicLink[0] != NULL && symbolicLink[1] != NULL;
            symbolicLink += wcslen(symbolicLink) + 1) {

            RtlInitUnicodeString(&objName, symbolicLink);

            DPF_ENTER(("VioAudioSaveData: Get object name: %wZ\n", &objName));

            ntStatus = IoGetDeviceObjectPointer(
                &objName,
                FILE_ALL_ACCESS,
                &fileObj,
                &devObj
            );

            if (NT_SUCCESS(ntStatus)) {
                ntStatus = ObOpenObjectByPointer(fileObj, OBJ_KERNEL_HANDLE, NULL, GENERIC_ALL,
                    NULL, KernelMode, &m_FileHandle);

                ObDereferenceObject(fileObj);

                if (!NT_SUCCESS(ntStatus)) {
                    DPF_ENTER(("VioAudioSaveData: ObOpenObjectByPointer Failed with status 0x%08x\n", ntStatus));
                    continue;
                }

                ntStatus = ObReferenceObjectByPointer(devObj, FILE_ALL_ACCESS, NULL, KernelMode);
                if (!NT_SUCCESS(ntStatus)) {
                    DPF_ENTER(("VioAudioSaveData: ObReferenceObjectByPointer Successed with status 0x%08x\n", ntStatus));
                    continue;
                }

                // if failed at this scope: do ObDereferenceObject(devObj);

                m_DevObj = devObj;

                DPF_ENTER(("VioAudioSaveData: IoGetDeviceObjectPointer Successed with status 0x%08x\n", ntStatus));
                break;
            }
            else {
                DPF_ENTER(("VioAudioSaveData: IoGetDeviceObjectPointer Failed with status 0x%08x\n", ntStatus));
            }
        }
    }

    ExFreePool(symbolicLinkList);

    DPF_ENTER(("VioAudioSaveData: FileOpen Final status 0x%08x\n", ntStatus));

    return ntStatus; // CSaveBackend::FileOpen(fOverWrite);
#else
    return CSaveBackend::FileOpen(fOverWrite);
#endif
}

NTSTATUS CVioSaveData::FileWrite(PBYTE pData, ULONG ulDataSize)
{
    return CSaveBackend::FileWrite(pData, ulDataSize);
}

NTSTATUS CVioSaveData::FileWriteHeader(void)
{
    return CSaveBackend::FileWriteHeader();
}

