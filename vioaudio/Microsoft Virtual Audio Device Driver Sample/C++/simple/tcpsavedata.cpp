#pragma warning (disable : 4127)
#pragma warning (disable : 26165)

#include <ntifs.h>

#include <msvad.h>
#include "savedata.h"
#include <ntstrsafe.h>   // This is for using RtlStringcbPrintf
#include <wdm.h>
#include "tcpsavedata.h"

CTcpSaveData::CTcpSaveData(_In_ PUNICODE_STRING RegistryPath) :
    CSaveBackend(RegistryPath), m_BufferMdl(NULL), m_Buffer(NULL), m_BufferSize(4096)
{
}

CTcpSaveData::~CTcpSaveData()
{
    if (m_FileHandle) {
        // FileClose();
    }

    if (m_BufferMdl != NULL) {
        IoFreeMdl(m_BufferMdl);
        m_BufferMdl = NULL;
    }

    if (m_Buffer != NULL) {
        ExFreePool(m_Buffer);
        m_Buffer = NULL;
    }
}

NTSTATUS CTcpSaveData::Initialize(ULONG StreamId, BOOL fCaptrue)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;

    static WskSampleServer g_Server(NULL);

    if (!m_Server) {
        m_Server = &g_Server;
    }

    UNREFERENCED_PARAMETER(StreamId);
    UNREFERENCED_PARAMETER(fCaptrue);

    if (!m_Server->isRun()) {
        ntStatus = m_Server->Start();
    }

    if (NT_SUCCESS(ntStatus)) {
        m_Buffer = ExAllocatePoolWithTag(
            NonPagedPool, m_BufferSize, WSKSAMPLE_BUFFER_POOL_TAG);
        if (m_Buffer == NULL){
            DPF_ENTER(("CTcpSaveData::Initialize: Failed to allocate buffer\n"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
        else {
            m_BufferMdl = IoAllocateMdl(
                m_Buffer,
                m_BufferSize, FALSE, FALSE, NULL);
            if (m_BufferMdl != NULL) {
                MmBuildMdlForNonPagedPool(m_BufferMdl);
            }
            else {
                DPF_ENTER(("CTcpSaveData::Initialize: Failed to allocate mdl\n"));
                ntStatus = STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    DPF_ENTER(("VioAudioSaveData: Initialize Final status 0x%08x\n", ntStatus));

    return ntStatus;
}

void CTcpSaveData::Disable
(
    BOOL fDisable
)
{
    UNREFERENCED_PARAMETER(fDisable);

    m_Server->Stop();
}

NTSTATUS CTcpSaveData::SetDataFormat(IN  PWAVEFORMATEX pWaveFormat)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;
    
    UNREFERENCED_PARAMETER(pWaveFormat);

    return ntStatus;
}

NTSTATUS CTcpSaveData::SetState
(
    _In_  KSSTATE           NewState
)
{
    LONG iState = (LONG)NewState;

    UNREFERENCED_PARAMETER(NewState);
    UNREFERENCED_PARAMETER(iState);

    return STATUS_SUCCESS;
}

void CTcpSaveData::SetVolume
(
    _In_  LONG              Channel,
    _In_  LONG              Value
)
{
    UNREFERENCED_PARAMETER(Channel);
    UNREFERENCED_PARAMETER(Value);
}

NTSTATUS CTcpSaveData::FileClose(void)
{
    DPF_ENTER(("CTcpSaveData::FileClose\n"));
    return STATUS_SUCCESS;
}

NTSTATUS CTcpSaveData::FileOpen(IN BOOL fOverWrite)
{
    UNREFERENCED_PARAMETER(fOverWrite);

    DPF_ENTER(("CTcpSaveData::FileOpen\n"));

    return STATUS_SUCCESS;
}

DWORD CTcpSaveData::FileRead
(
    _Out_writes_bytes_(ulBufferSize)  PBYTE pBuffer,
    _In_                            ULONG   ulBufferSize
)
{
    // for irq_level <= DISPATCH_LEVEL

    DWORD dwsBytesReturned = 0;

    UNREFERENCED_PARAMETER(pBuffer);
    UNREFERENCED_PARAMETER(ulBufferSize);

    DPF_ENTER(("CTcpSaveData::FileRead %lu\n", dwsBytesReturned));

    return dwsBytesReturned;
}

NTSTATUS CTcpSaveData::FileWrite(PBYTE pData, ULONG ulDataSize)
{
    WSK_BUF wskBuf = { 0 };

    ULONG ulToProcess = ulDataSize, ulChunk = 0, ulStart = 0, ulProcessed = 0;

    wskBuf.Mdl = m_BufferMdl;

    while (ulToProcess) {
        ulChunk = min(ulToProcess, m_BufferSize - ulStart);

        RtlCopyMemory((PBYTE)m_Buffer + ulStart, pData + ulProcessed, ulChunk);

        wskBuf.Offset = ulStart;
        wskBuf.Length = ulChunk;

        m_Server->SendData(&wskBuf, TRUE);

        ulProcessed += ulChunk;
        ulStart = ulProcessed % ulChunk;
        ulToProcess -= ulChunk;
    }

    DPF_ENTER(("CTcpSaveData::FileWrite %lu\n", ulDataSize));

    return STATUS_SUCCESS;
}

NTSTATUS CTcpSaveData::FileWriteHeader(void)
{
    return STATUS_SUCCESS;
}

void
CTcpSaveData::SetMute
(
    _In_  BOOL              Value
)
{
    UNREFERENCED_PARAMETER(Value);
}

BOOL
CTcpSaveData::GetMute
(
)
{
    return FALSE;
}

