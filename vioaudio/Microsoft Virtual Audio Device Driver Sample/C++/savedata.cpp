/*++

Copyright (c) 1997-2000  Microsoft Corporation All Rights Reserved

Module Name:

    savedata.cpp

Abstract:

    Implementation of MSVAD data saving class.

    To save the playback data to disk, this class maintains a circular data
    buffer, associated frame structures and worker items to save frames to
    disk.
    Each frame structure represents a portion of buffer. When that portion
    of frame is full, a workitem is scheduled to save it to disk.



--*/
#pragma warning (disable : 4127)
#pragma warning (disable : 26165)

#include <msvad.h>
#include "savedata.h"
#include <ntstrsafe.h>   // This is for using RtlStringcbPrintf

//=============================================================================
// Defines
//=============================================================================
#define RIFF_TAG                    0x46464952;
#define WAVE_TAG                    0x45564157;
#define FMT__TAG                    0x20746D66;
#define DATA_TAG                    0x61746164;

#define DEFAULT_FRAME_COUNT         (1024)
#define DEFAULT_FRAME_SIZE          (2048) // (1764 1920 * 10)
#define DEFAULT_BUFFER_SIZE         (DEFAULT_FRAME_SIZE * DEFAULT_FRAME_COUNT)

#define DEFAULT_FILE_NAME           L"\\DosDevices\\C:\\STREAM"

#define MAX_WORKER_ITEM_PLAYBACK_COUNT       1 // must be one
#define MAX_WORKER_ITEM_RECORD_COUNT         1
#define MAX_WORKER_ITEM_COUNT                (MAX_WORKER_ITEM_PLAYBACK_COUNT + MAX_WORKER_ITEM_RECORD_COUNT)

//=============================================================================
// Statics
//=============================================================================
ULONG CSaveData::m_ulStreamId = 0;

// #define LATENCY_DEBUG
#ifdef LATENCY_DEBUG
#define DbgLatencyStart(...) LARGE_INTEGER beginTime, endTime;                      \
    KeQuerySystemTime(&beginTime);

#define DbgLatencyEnd(beginTime, endTime, format, ...) do{		                    \
  KeQuerySystemTime(&endTime);			                                            \
  LARGE_INTEGER Duration;                                                           \
  Duration.QuadPart = (endTime.QuadPart - beginTime.QuadPart) / (10000);		    \
  DbgPrint("[vadsimp] " __FUNCTION__ ":"format" Elapsed %lld ms %s\n",##__VA_ARGS__, Duration.QuadPart, Duration.QuadPart > 10 ? ", very very very big latency ...":"");  \
}while(0)
#else
#define DbgLatencyStart(...)
#define DbgLatencyEnd(...)
#endif // LATENCY_DEBUG


PCSaveBackend CSaveData::m_SaveBackends[MAX_NR_SAVE_BACKEND] = { 0 };
INT CSaveData::m_SaveBackendsLength = 0;

static ULONG RoundupPowOfTwo(ULONG x)
{
    x--;

    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    x++;

    return x;
}

void ring_buffer_lock_init(ring_buffer_t *rbuf)
{
    UNREFERENCED_PARAMETER(rbuf);

    KeInitializeMutex(&rbuf->lock, 1);
}

void ring_buffer_lock(ring_buffer_t *rbuf)
{
    UNREFERENCED_PARAMETER(rbuf);

    if (STATUS_SUCCESS == KeWaitForSingleObject (
        &rbuf->lock,
        Executive,
        KernelMode,
        FALSE,
        NULL
    ))
    {

    }
}

void ring_buffer_unlock(ring_buffer_t *rbuf)
{
    UNREFERENCED_PARAMETER(rbuf);

    KeReleaseMutex(&rbuf->lock, FALSE);
}

int ring_buffer_init(ring_buffer_t *rbuf, uint8_t *data, size_t len)
{
    UNREFERENCED_PARAMETER(rbuf);
    UNREFERENCED_PARAMETER(data);
    UNREFERENCED_PARAMETER(len);

#if 1
    if (roundup_pow_of_two((ULONG)len) != (ULONG)len) {
        // assert(0);
        return -1;
    }

    rbuf->rpos = rbuf->wpos = 0;
    rbuf->data = data;
    rbuf->size = (uint32_t)len;
    rbuf->size_mask = rbuf->size - 1;

    ring_buffer_lock_init(rbuf);
#endif

    return 0;
}

uint32_t ring_buffer_free(ring_buffer_t *rbuf)
{
    int32_t free;

    free = rbuf->rpos - rbuf->wpos;
    if (free <= 0)
        free += rbuf->size;

    return (uint32_t)(free/* - 1*/);
}


uint32_t ring_buffer_avail(ring_buffer_t *rbuf)
{
    int32_t avail;

    avail = rbuf->wpos - rbuf->rpos;
    if (avail < 0)
        avail += rbuf->size;

    return (uint32_t)avail;
}

int ring_buffer_empty(ring_buffer_t *rbuf)
{
    return (rbuf->rpos == rbuf->wpos);
}

int ring_buffer_full(ring_buffer_t *rbuf)
{
    return ring_buffer_avail(rbuf) == rbuf->size;
}

void ring_buffer_flush(ring_buffer_t *rbuf)
{
    rbuf->rpos = rbuf->wpos;
}

int ring_buffer_write(ring_buffer_t *rbuf, const uint8_t* buf, size_t leng)
{
    size_t to_transfer, chunk;
    uint32_t wpos, start, copied = 0;

    to_transfer = ring_buffer_free(rbuf);
    to_transfer = min(to_transfer, leng);

    wpos = rbuf->wpos;

    while (to_transfer) {
        start = wpos & rbuf->size_mask;
        chunk = min(to_transfer, rbuf->size - start);

        memcpy(rbuf->data + start, buf + copied, chunk);

        to_transfer -= chunk;
        copied += chunk;
        wpos += chunk;
    }

    rbuf->wpos += copied;
    rbuf->wpos &= rbuf->size_mask;

    return copied;
}

typedef int(*consume_rbuf_pft)(uint8_t* buf, size_t len, void *other);

int ring_buffer_consume(ring_buffer_t *rbuf, size_t leng, consume_rbuf_pft callback, void *args)
{
    size_t to_transfer, chunk;
    uint32_t rpos, start, copied = 0;

    to_transfer = ring_buffer_avail(rbuf);
    to_transfer = min(to_transfer, leng);

    rpos = rbuf->rpos;

    while (to_transfer) {
        start = rpos & rbuf->size_mask;
        chunk = min(to_transfer, rbuf->size - start);

        if (callback && callback(rbuf->data + start, chunk, args) < 0) {
            break;
        }

        to_transfer -= chunk;
        copied      += chunk;
        rpos        += chunk;
    }

    rbuf->rpos += copied;
    rbuf->rpos &= rbuf->size_mask;

    return copied;
}

int ring_buffer_read(ring_buffer_t *rbuf, uint8_t* buf, size_t leng)
{
    size_t to_transfer, chunk;
    uint32_t rpos, start, copied = 0;

    to_transfer = ring_buffer_avail(rbuf);
    to_transfer = min(to_transfer, leng);

    rpos = rbuf->rpos;

    while (to_transfer) {
        start = rpos & rbuf->size_mask;
        chunk = min(to_transfer, rbuf->size - start);

        memcpy(buf, rbuf->data + start, chunk);

        to_transfer -= chunk;
        copied += chunk;
        rpos += chunk;
    }

    rbuf->rpos += copied;
    rbuf->rpos &= rbuf->size_mask;

    return copied;
}


uint32_t ring_buffer_free_locked(ring_buffer_t *rbuf)
{
    uint32_t free;

    ring_buffer_lock(rbuf);
    free = ring_buffer_free(rbuf);
    ring_buffer_unlock(rbuf);

    return free;
}


uint32_t ring_buffer_avail_locked(ring_buffer_t *rbuf)
{
    uint32_t avail;

    ring_buffer_lock(rbuf);
    avail = ring_buffer_avail(rbuf);
    ring_buffer_unlock(rbuf);

    return avail;
}

int ring_buffer_empty_locked(ring_buffer_t *rbuf)
{
    int empty;

    ring_buffer_lock(rbuf);
    empty = ring_buffer_empty(rbuf);
    ring_buffer_unlock(rbuf);

    return empty;
}

int ring_buffer_full_locked(ring_buffer_t *rbuf)
{
    int full;

    ring_buffer_lock(rbuf);
    full = ring_buffer_full(rbuf);
    ring_buffer_unlock(rbuf);

    return full;
}

void ring_buffer_flush_locked(ring_buffer_t *rbuf)
{
    ring_buffer_lock(rbuf);
    ring_buffer_flush(rbuf);
    ring_buffer_unlock(rbuf);
}

int ring_buffer_write_locked(ring_buffer_t *rbuf, const uint8_t* buf, size_t leng)
{
    uint32_t nwrite;

    ring_buffer_lock(rbuf);
    nwrite = ring_buffer_write(rbuf, buf, leng);
    ring_buffer_unlock(rbuf);

    return nwrite;
}

int ring_buffer_read_locked(ring_buffer_t *rbuf, uint8_t* buf, size_t leng)
{
    uint32_t nread;

    ring_buffer_lock(rbuf);
    nread = ring_buffer_read(rbuf, buf, leng);
    ring_buffer_unlock(rbuf);

    return nread;
}


#ifdef WORTK_THREAD_MODE
VOID
SendPcmRoutine(
    IN PVOID pContext
)
{
    PCSaveData pSaveData = (PCSaveData)(pContext);

    NTSTATUS status = STATUS_SUCCESS;

    DPF_ENTER(("CSaveData::SendPcmRoutine\n"));

    for (; ; ) {
        status = KeWaitForSingleObject(&pSaveData->m_WakeUpWorker, Executive,
            KernelMode, FALSE, NULL);

        if (STATUS_WAIT_0 == status) {

            if (pSaveData->m_bShutDownWorker) {
                DPF_ENTER(("CSaveData::CreateWorkerThread Exiting Thread!...\n"));
                break;
            }
            else {
                pSaveData->SendData();
            }
        }
    }

    DPF_ENTER(("CSaveData::CreateWorkerThread Thread about to exit...\n"));

    PsTerminateSystemThread(STATUS_SUCCESS);
}

#endif // WORTK_THREAD_MODE

NTSTATUS
CreateWorkerThread(
    _Inout_ PWrokerContext WokerCtx
)
{
    NTSTATUS            status = STATUS_SUCCESS;
    HANDLE              hThread = 0;
    OBJECT_ATTRIBUTES   oa;

    DPF_ENTER(("CSaveData::CreateWorkerThread\n"));

    KeInitializeEvent(&WokerCtx->WakeUp,
        SynchronizationEvent,
        FALSE
    );

    WokerCtx->bShutDown = FALSE;
    WokerCtx->bInited = TRUE;

    if (NULL == WokerCtx->Worker) {
        InitializeObjectAttributes(&oa, NULL,
            OBJ_KERNEL_HANDLE, NULL, NULL);

        status = PsCreateSystemThread(&hThread, THREAD_ALL_ACCESS, &oa, NULL, NULL,
            WokerCtx->StartRoutine, WokerCtx);

        if (!NT_SUCCESS(status)) {
            DPF_ENTER(("CSaveData::CreateWorkerThread failed to create worker thread status 0x%08x\n", status));
            return status;
        }

        ObReferenceObjectByHandle(hThread, THREAD_ALL_ACCESS, NULL,
            KernelMode, (PVOID*)&WokerCtx->Worker, NULL);
        // KeSetPriorityThread(WokerCtx->Worker, HIGH_PRIORITY);

        ZwClose(hThread);
    }

    KeSetEvent(&WokerCtx->WakeUp, EVENT_INCREMENT, FALSE);

    return status;
}

VOID
ShutdownWorkerThread(
    _In_ PWrokerContext WokerCtx
)
{
    NTSTATUS ntStatus;

    if (WokerCtx->bInited && WokerCtx->Worker) {
        WokerCtx->bShutDown = TRUE;

        ntStatus = KeWaitForSingleObject(WokerCtx->Worker, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(ntStatus)){
            DPF_ENTER(("CSaveData::KeWaitForSingleObject didn't succeed status 0x%08x\n", ntStatus));
        }

        ObDereferenceObject(WokerCtx->Worker);
        WokerCtx->Worker = NULL;
    }
}

VOID
ReadPcmRoutine(
    IN PVOID pContext
)
{
    PWrokerContext pWorkerCtx = (PWrokerContext)pContext;
    PCSaveData pSaveData = (PCSaveData)(pWorkerCtx->pArgs);
    DWORD dwBytesReturned = 0;

    LARGE_INTEGER interval;
    interval.QuadPart = -100 * 1000; // *1000;

    DPF_ENTER(("CSaveData::ReadPcmRoutine\n"));

    for (; ; ) {
        if (pWorkerCtx->bShutDown) {
            DPF_ENTER(("CSaveData::ReadPcmRoutine Exiting Thread!...\n"));
            break;
        }
        else {
            INT64 i64Wpos, i64Rpos;
            INT64 i64ToTransfer;
            UINT32 uiChunk, uiStart;

            i64Wpos = InterlockedAdd((LONG *)&pSaveData->m_i64Wpos, 0);
            i64Rpos = InterlockedAdd((LONG *)&pSaveData->m_i64Rpos, 0);

            i64ToTransfer = i64Rpos - i64Wpos;
            if (i64ToTransfer <= 0) {
                i64ToTransfer += pSaveData->m_ulBufferSize;
            }
            i64ToTransfer = min(i64ToTransfer, 8192); // should calc from bytes per ms

            while (i64ToTransfer) {
                uiStart = (UINT32)(i64Wpos & pSaveData->m_ulBufferSizeMask);
                uiChunk = (UINT32)min(i64ToTransfer, pSaveData->m_ulBufferSize - uiStart);
            
                dwBytesReturned = pSaveData->FileRead(pSaveData->m_pDataBuffer, uiChunk);
                if (!dwBytesReturned) {
                    break;
                }

                DPF_ENTER(("CSaveData::ReadPcmRoutine dwBytesReturned=%lu\n", dwBytesReturned));

                i64Wpos       += dwBytesReturned;
                i64ToTransfer -= dwBytesReturned;
            }

            InterlockedExchange((LONG *)&pSaveData->m_i64Wpos, (LONG)(i64Wpos & pSaveData->m_ulBufferSizeMask));
        }

        DPF_ENTER(("CSaveData::ReadPcmRoutine before delay...\n"));
        KeDelayExecutionThread(KernelMode, FALSE, &interval);
        DPF_ENTER(("CSaveData::ReadPcmRoutine after delay...\n"));
    }

    DPF_ENTER(("CSaveData::ReadPcmRoutine Thread about to exit...\n"));

    PsTerminateSystemThread(STATUS_SUCCESS);
}

#pragma code_seg("PAGE")
//=============================================================================
// CSaveData
//=============================================================================

//=============================================================================
CSaveData::CSaveData()
    : m_pDataBuffer(NULL),
    m_FileHandle(NULL),
    m_ulFrameCount(DEFAULT_FRAME_COUNT),
    m_ulBufferSize(RoundupPowOfTwo(DEFAULT_BUFFER_SIZE)),
    m_ulBufferSizeMask(m_ulBufferSize - 1),
    m_ulFrameSize((DEFAULT_FRAME_SIZE)),
    m_ulTransferChunkSize(m_ulFrameSize),
    m_ulBufferPtr(0),
    m_ulFramePtr(0),
    m_fFrameUsed(NULL),
    m_pFilePtr(NULL),
    m_fWriteDisabled(FALSE),
    m_bInitialized(FALSE),
    m_i64Wpos(0),
    m_i64Rpos(0)
{

    PAGED_CODE();

    m_waveFormat = NULL;
    m_FileHeader.dwRiff           = RIFF_TAG;
    m_FileHeader.dwFileSize       = 0;
    m_FileHeader.dwWave           = WAVE_TAG;
    m_FileHeader.dwFormat         = FMT__TAG;
    m_FileHeader.dwFormatLength   = sizeof(WAVEFORMATEX);

    m_DataHeader.dwData           = DATA_TAG;
    m_DataHeader.dwDataLength     = 0;

    RtlZeroMemory(&m_objectAttributes, sizeof(m_objectAttributes));
    RtlZeroMemory(&m_RecorderCtx, sizeof(m_RecorderCtx));

    m_ulStreamId++;
    InitializeWorkItems(GetDeviceObject());
} // CSaveData

//=============================================================================
CSaveData::~CSaveData()
{
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::~CSaveData]"));

#ifdef WORTK_THREAD_MODE
    m_bShutDownWorker = TRUE;
    KeSetEvent(&this->m_WakeUpWorker, EVENT_INCREMENT, FALSE);

    // wait for thread terminate: KeWaitForSingleObject
    LARGE_INTEGER interval;
    interval.QuadPart = -10 * 1000 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
#else
    DestroyWorkItems();
#endif

    ShutdownWorkerThread(&m_RecorderCtx);

    // Update the wave header in data file with real file size.
    //
    if(m_pFilePtr)
    {
        m_FileHeader.dwFileSize =
            (DWORD) m_pFilePtr->QuadPart - 2 * sizeof(DWORD);
        m_DataHeader.dwDataLength = (DWORD) m_pFilePtr->QuadPart -
                                     sizeof(m_FileHeader)        -
                                     m_FileHeader.dwFormatLength -
                                     sizeof(m_DataHeader);

        if (STATUS_SUCCESS == KeWaitForSingleObject
            (
                &m_FileSync,
                Executive,
                KernelMode,
                FALSE,
                NULL
            ))
        {
            if (NT_SUCCESS(FileOpen(FALSE)))
            {
                FileWriteHeader();

                FileClose();
            }

            KeReleaseMutex(&m_FileSync, FALSE);
        }
    }

    FileClose();

    if (m_waveFormat)
    {
        ExFreePoolWithTag(m_waveFormat, MSVAD_POOLTAG);
    }

    if (m_fFrameUsed)
    {
        ExFreePoolWithTag(m_fFrameUsed, MSVAD_POOLTAG);

        // NOTE : Do not release m_pFilePtr.
    }

    if (m_FileName.Buffer)
    {
        ExFreePoolWithTag(m_FileName.Buffer, MSVAD_POOLTAG);
    }

    if (m_pDataBuffer)
    {
        ExFreePoolWithTag(m_pDataBuffer, MSVAD_POOLTAG);
    }
} // CSaveData


//=============================================================================
BOOLEAN CSaveData::AddSaveBackend
(
    PCSaveBackend SaveBackend
)
{
    /* ARRAYSIZE(m_SaveBackends) */
    if (m_SaveBackendsLength > MAX_NR_SAVE_BACKEND) {
        return FALSE;
    }

    m_SaveBackends[m_SaveBackendsLength++] = SaveBackend;

    return TRUE;
} // AddSaveBackend

//=============================================================================
BOOLEAN CSaveData::RemoveAllSaveBackend
(
    void
)
{
    for (int i = 0; i < m_SaveBackendsLength; i++) {

        delete m_SaveBackends[i];

        m_SaveBackends[i] = NULL;
    }

    m_SaveBackendsLength = 0;

    return TRUE;
} // AddSaveBackend

//=============================================================================
NTSTATUS CSaveData::SetState
(
    _In_  KSSTATE                 NewState
)
{
    NTSTATUS ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("CSaveData::SetState\n"));

    switch ((NewState))
    {
    case KSSTATE_STOP:
        DPF_ENTER(("CSaveData::SetState KSSTATE_STOP\n"));
        break;
    case KSSTATE_ACQUIRE:
        DPF_ENTER(("CSaveData::SetState KSSTATE_ACQUIRE\n"));
        break;
    case KSSTATE_PAUSE:
        DPF_ENTER(("CSaveData::SetState KSSTATE_PAUSE\n"));
        break;
    case KSSTATE_RUN:
        DPF_ENTER(("CSaveData::SetState KSSTATE_RUN\n"));
        break;
    default:
        break;
    }

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->SetState(NewState);
            if (!NT_SUCCESS(ntStatus)) {
                break;
            }
        }
    }

    return ntStatus;
} // SetState

//=============================================================================
void CSaveData::SetVolume
(
    _In_  ULONG             Index,
    _In_  LONG              Channel,
    _In_  LONG              Value
)
{
    UNREFERENCED_PARAMETER(Index);

    DPF_ENTER(("VioAudioSaveData: Set Channel %ld volume %ld\n", Channel, Value));

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            m_SaveBackends[i]->SetVolume(Channel, Value);
        }
    }

} // SetVolume

//=============================================================================
LONG CSaveData::GetVolume
(
    _In_  ULONG             Index,
    _In_  LONG              Channel
)
{
    UNREFERENCED_PARAMETER(Index);
    UNREFERENCED_PARAMETER(Channel);

    DPF_ENTER(("VioAudioSaveData: Get volume\n"));

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            return m_SaveBackends[i]->GetVolume(Channel);
        }
    }

    return 0;
} // GetVolume

//=============================================================================
void CSaveData::SetMute
(
    _In_  ULONG             Index,
    _In_  BOOL              Value
)
{
    UNREFERENCED_PARAMETER(Index);

    DPF_ENTER(("[VioAudioSaveData::SetMute] %d", Value));
    
    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            m_SaveBackends[i]->SetMute(Value);
        }
    }
} // SetMute

//=============================================================================
BOOL CSaveData::GetMute
(
    _In_  ULONG             Index
)
{
    UNREFERENCED_PARAMETER(Index);

    DPF_ENTER(("VioAudioSaveData: GetMute\n"));

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            return m_SaveBackends[i]->GetMute();
        }
    }

    return FALSE;
} // GetMute

//=============================================================================
void
CSaveData::DestroyWorkItems
(
    void
)
{
    PAGED_CODE();

    if (m_pWorkItems)
    {
        //frees the work items
        for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++)
        {

            if (m_pWorkItems[i].WorkItem != NULL)
            {
                IoFreeWorkItem(m_pWorkItems[i].WorkItem);
                m_pWorkItems[i].WorkItem = NULL;
            }
        }

        ExFreePoolWithTag(m_pWorkItems, MSVAD_POOLTAG);
        m_pWorkItems = NULL;
    }

} // DestroyWorkItems

//=============================================================================
void
CSaveData::Disable
(
    BOOL                        fDisable
)
{
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::Disable fDisable=%d]", fDisable));

    m_fWriteDisabled = fDisable;

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            m_SaveBackends[i]->Disable(fDisable);
        }
    }
} // Disable

//=============================================================================
NTSTATUS
CSaveData::FileClose(void)
{
    PAGED_CODE();

    NTSTATUS                    ntStatus = STATUS_SUCCESS;

    if (m_FileHandle)
    {
        ntStatus = ZwClose(m_FileHandle);
        m_FileHandle = NULL;
    }

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileClose();

            if (!NT_SUCCESS(ntStatus)) {
                DPF_ENTER(("[CSaveData::m_SaveBackends %d FileClose Error: 0x%08x]", i, ntStatus));
                break;
            }
        }
    }

    return ntStatus;
} // FileClose

//=============================================================================
NTSTATUS
CSaveData::FileOpen
(
    IN  BOOL                    fOverWrite
)
{
    // PAGED_CODE();

    NTSTATUS                    ntStatus = STATUS_SUCCESS;
    IO_STATUS_BLOCK             ioStatusBlock;

    UNREFERENCED_PARAMETER(ioStatusBlock);

    if( FALSE == m_bInitialized )
    {
        return STATUS_UNSUCCESSFUL;
    }

#if 0
    if(!m_FileHandle)
    {
        ntStatus =
            ZwCreateFile
            (
                &m_FileHandle,
                GENERIC_WRITE | SYNCHRONIZE,
                &m_objectAttributes,
                &ioStatusBlock,
                NULL,
                FILE_ATTRIBUTE_NORMAL,
                0,
                fOverWrite ? FILE_OVERWRITE_IF : FILE_OPEN_IF,
                FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                NULL,
                0
            );
        if (!NT_SUCCESS(ntStatus))
        {
            DPF(D_TERSE, ("[CSaveData::FileOpen : Error opening data file]"));
            return ntStatus;
        }
    }
#endif

    /************************************************/
    // m_FileHandle = NULL; // force assign null
    /************************************************/

    INT openedCnt = 0;

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileOpen(fOverWrite);

            if (!NT_SUCCESS(ntStatus)) {
                DPF_ENTER(("[CSaveData::m_SaveBackends %d FileOpen Error: 0x%08x]", i, ntStatus));
                continue;
            }
            
            openedCnt += 1;
        }
    }

    ntStatus = (openedCnt > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    return ntStatus;
} // FileOpen

//=============================================================================
NTSTATUS
CSaveData::FileWrite
(
    _In_reads_bytes_(ulDataSize)    PBYTE   pData,
    _In_                            ULONG   ulDataSize
)
{
    PAGED_CODE();

    ASSERT(pData);
    ASSERT(m_pFilePtr);

    NTSTATUS                    ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("[CSaveData::FileWrite ulDataSize=%lu]", ulDataSize));

#if 0
    if (m_FileHandle)
    {
        IO_STATUS_BLOCK         ioStatusBlock;

        ntStatus = ZwWriteFile( m_FileHandle,
                                NULL,
                                NULL,
                                NULL,
                                &ioStatusBlock,
                                pData,
                                ulDataSize,
                                m_pFilePtr,
                                NULL);

        if (NT_SUCCESS(ntStatus))
        {
            ASSERT(ioStatusBlock.Information == ulDataSize);

            m_pFilePtr->QuadPart += ulDataSize;
        }
        else
        {
            DPF(D_TERSE, ("[CSaveData::FileWrite : WriteFileError]"));
        }
    }
    else
    {
        DPF(D_TERSE, ("[CSaveData::FileWrite : File not open]"));
        ntStatus = STATUS_INVALID_HANDLE;
    }
#endif

    static LARGE_INTEGER prevTime;
    LARGE_INTEGER currTime, now;

#define LOG_LATENTCY(...) DPF_ENTER((__VA_ARGS__))

    KeQuerySystemTime(&currTime);

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileWrite(pData, ulDataSize);

            if (!NT_SUCCESS(ntStatus)) {
                DPF(D_TERSE, ("[CSaveData::m_SaveBackends %d WriteFile Error: 0x%08x]", i, ntStatus));
                continue;
            }
        }
    }

    KeQuerySystemTime(&now);

    LOG_LATENTCY("CSaveData::FileWrite all prev elapsed %lld ms, current elapsed %lld ms\n", 
        (currTime.QuadPart - prevTime.QuadPart) / 10000, (now.QuadPart - currTime.QuadPart) / 10000);

    prevTime = currTime;

    return ntStatus;
} // FileWrite

DWORD
CSaveData::FileRead
(
    _Inout_updates_bytes_all_(ulByteCount)  PBYTE   pBuffer,
    _In_                                    ULONG   ulByteCount
)
{
    DWORD dwBytesReturned = 0;

    DPF_ENTER(("[CSaveData::FileRead] ulByteCount=%lu", ulByteCount));

    // -> &= or |=

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            dwBytesReturned = m_SaveBackends[i]->FileRead(pBuffer, ulByteCount);

            if (dwBytesReturned) {
                break;
            }
        }
    }

    return dwBytesReturned;
} // FileRead

//=============================================================================
NTSTATUS
CSaveData::FileWriteHeader(void)
{
    PAGED_CODE();

    NTSTATUS                    ntStatus = STATUS_SUCCESS;

#if 0
    if (m_FileHandle && m_waveFormat)
    {
        IO_STATUS_BLOCK         ioStatusBlock;

        m_pFilePtr->QuadPart = 0;

        m_FileHeader.dwFormatLength = (m_waveFormat->wFormatTag == WAVE_FORMAT_PCM) ?
                                        sizeof( PCMWAVEFORMAT ) :
                                        sizeof( WAVEFORMATEX ) + m_waveFormat->cbSize;

        ntStatus = ZwWriteFile( m_FileHandle,
                                NULL,
                                NULL,
                                NULL,
                                &ioStatusBlock,
                                &m_FileHeader,
                                sizeof(m_FileHeader),
                                m_pFilePtr,
                                NULL);
        if (!NT_SUCCESS(ntStatus))
        {
            DPF(D_TERSE, ("[CSaveData::FileWriteHeader : Write File Header Error]"));
        }

        m_pFilePtr->QuadPart += sizeof(m_FileHeader);

        ntStatus = ZwWriteFile( m_FileHandle,
                                NULL,
                                NULL,
                                NULL,
                                &ioStatusBlock,
                                m_waveFormat,
                                m_FileHeader.dwFormatLength,
                                m_pFilePtr,
                                NULL);
        if (!NT_SUCCESS(ntStatus))
        {
            DPF(D_TERSE, ("[CSaveData::FileWriteHeader : Write Format Error]"));
        }

        m_pFilePtr->QuadPart += m_FileHeader.dwFormatLength;

        ntStatus = ZwWriteFile( m_FileHandle,
                                NULL,
                                NULL,
                                NULL,
                                &ioStatusBlock,
                                &m_DataHeader,
                                sizeof(m_DataHeader),
                                m_pFilePtr,
                                NULL);
        if (!NT_SUCCESS(ntStatus))
        {
            DPF(D_TERSE, ("[CSaveData::FileWriteHeader : Write Data Header Error]"));
        }

        m_pFilePtr->QuadPart += sizeof(m_DataHeader);
    }
    else
    {
        DPF(D_TERSE, ("[CSaveData::FileWriteHeader : File not open]"));
        ntStatus = STATUS_INVALID_HANDLE;
    }
#endif

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileWriteHeader();

            if (!NT_SUCCESS(ntStatus)) {
                DPF(D_TERSE, ("[CSaveData::m_SaveBackends %d FileWriteHeader Error: 0x%08x]", i, ntStatus));
                break;
            }
        }
    }

    return ntStatus;
} // FileWriteHeader
NTSTATUS
CSaveData::SetDeviceObject
(
    IN  PDEVICE_OBJECT          DeviceObject
)
{
    PAGED_CODE();

    ASSERT(DeviceObject);

    NTSTATUS                    ntStatus = STATUS_SUCCESS;
    
    m_pDeviceObject = DeviceObject;
    return ntStatus;
}

PDEVICE_OBJECT
CSaveData::GetDeviceObject
(
    void
)
{
    PAGED_CODE();

    return m_pDeviceObject;
}

#pragma code_seg()
//=============================================================================
PSAVEWORKER_PARAM
CSaveData::GetNewWorkItem
(
    WORKITEM_TYPE Type
)
{
    LARGE_INTEGER               timeOut = { 0 };
    NTSTATUS                    ntStatus;
    int                         start = 0, end = MAX_WORKER_ITEM_COUNT;

    DPF_ENTER(("[CSaveData::GetNewWorkItem]"));

    if (m_pWorkItems == NULL) {
        return NULL;
    }

    switch (Type) {
    case WORKITEM_TYPE_PLAYBACK:
        start = 0;
        end = MAX_WORKER_ITEM_PLAYBACK_COUNT;
        break;
    case WORKITEM_TYPE_RECORD:
        start = MAX_WORKER_ITEM_PLAYBACK_COUNT;
        end = start + MAX_WORKER_ITEM_RECORD_COUNT;
        break;
    default:
        start = 0;
        end = MAX_WORKER_ITEM_COUNT;
        break;
    }

    for (int i = start; i < end; i++)
    {
        ntStatus =
            KeWaitForSingleObject
            (
                &m_pWorkItems[i].EventDone,
                Executive,
                KernelMode,
                FALSE,
                &timeOut
            );
        if (STATUS_SUCCESS == ntStatus)
        {
            if (m_pWorkItems[i].WorkItem)
                return &(m_pWorkItems[i]);
            else
                return NULL;
        }
    }

    return NULL;
} // GetNewWorkItem
#pragma code_seg("PAGE")

//=============================================================================
NTSTATUS
CSaveData::Initialize
(
    BOOL fCaptrue,
    ULONG ulAvgBytesPerSec
)
{
    PAGED_CODE();

    NTSTATUS    ntStatus = STATUS_SUCCESS;
    WCHAR       szTemp[MAX_PATH];
    size_t      cLen;

    DPF_ENTER(("[CSaveData::Initialize]"));

    if (fCaptrue) {
        // !!! TODO !!!
    }

    m_ulTransferChunkSize = ulAvgBytesPerSec / 100;

    // Allocaet data file name.
    //
    RtlStringCchPrintfW(szTemp, MAX_PATH, L"%s_%d.wav", DEFAULT_FILE_NAME, m_ulStreamId);
    m_FileName.Length = 0;
    ntStatus = RtlStringCchLengthW (szTemp, sizeof(szTemp)/sizeof(szTemp[0]), &cLen);
    if (NT_SUCCESS(ntStatus))
    {
        m_FileName.MaximumLength = (USHORT)((cLen * sizeof(WCHAR)) +  sizeof(WCHAR));//convert to wchar and add room for NULL
        m_FileName.Buffer = (PWSTR)
            ExAllocatePoolWithTag
            (
                PagedPool,
                m_FileName.MaximumLength,
                MSVAD_POOLTAG
            );
        if (!m_FileName.Buffer)
        {
            DPF(D_TERSE, ("[Could not allocate memory for FileName]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Allocate memory for data buffer.
    //
    if (NT_SUCCESS(ntStatus))
    {
        RtlStringCbCopyW(m_FileName.Buffer, m_FileName.MaximumLength, szTemp);
        m_FileName.Length = (USHORT)wcslen(m_FileName.Buffer) * sizeof(WCHAR);
        DPF(D_BLAB, ("[New DataFile -- %S", m_FileName.Buffer));

        m_pDataBuffer = (PBYTE)
            ExAllocatePoolWithTag
            (
                NonPagedPool,
                m_ulBufferSize,
                MSVAD_POOLTAG
            );
        if (!m_pDataBuffer)
        {
            DPF(D_TERSE, ("[Could not allocate memory for Saving Data]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
        else {
            ring_buffer_init(&m_rbuf, m_pDataBuffer, m_ulBufferSize);
        }
    }

    // Allocate memory for frame usage flags and m_pFilePtr.
    //
    if (NT_SUCCESS(ntStatus))
    {
        m_fFrameUsed = (PBOOL)
            ExAllocatePoolWithTag
            (
                NonPagedPool,
                m_ulFrameCount * sizeof(BOOL) +
                sizeof(LARGE_INTEGER),
                MSVAD_POOLTAG
            );
        if (!m_fFrameUsed)
        {
            DPF(D_TERSE, ("[Could not allocate memory for frame flags]"));
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    // Initialize the spinlock to synchronize access to the frames
    //
    KeInitializeSpinLock ( &m_FrameInUseSpinLock ) ;

    // Initialize the file mutex
    //
    KeInitializeMutex( &m_FileSync, 1 ) ;

    // Open the data file.
    //
    if (NT_SUCCESS(ntStatus))
    {
        // m_fFrameUsed has additional memory to hold m_pFilePtr
        //
        m_pFilePtr = (PLARGE_INTEGER)
            (((PBYTE) m_fFrameUsed) + m_ulFrameCount * sizeof(BOOL));
        RtlZeroMemory(m_fFrameUsed, m_ulFrameCount * sizeof(BOOL) + sizeof(LARGE_INTEGER));

        // Create data file.
        InitializeObjectAttributes
        (
            &m_objectAttributes,
            &m_FileName,
            OBJ_CASE_INSENSITIVE|OBJ_KERNEL_HANDLE,
            NULL,
            NULL
        );

        m_bInitialized = TRUE;

        // Write wave header information to data file.
        ntStatus = KeWaitForSingleObject
            (
                &m_FileSync,
                Executive,
                KernelMode,
                FALSE,
                NULL
            );

        if (STATUS_SUCCESS == ntStatus)
        {
            ntStatus = FileOpen(TRUE);
            if (NT_SUCCESS(ntStatus))
            {
                ntStatus = FileWriteHeader();

                // FileClose();
            }

            KeReleaseMutex( &m_FileSync, FALSE );
        }
    }

    m_RecorderCtx.bShutDown = FALSE;
    m_RecorderCtx.StartRoutine = ReadPcmRoutine;
    m_RecorderCtx.pArgs = this;

    // CreateWorkerThread(&m_RecorderCtx);

    INT initedCnt = 0;

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->Initialize(m_ulStreamId, fCaptrue);

            if (NT_SUCCESS(ntStatus)) {
                initedCnt += 1;
            }
        }
    }

    ntStatus = (initedCnt > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

#ifdef WORTK_THREAD_MODE
    KeInitializeEvent(&m_WakeUpWorker,
        SynchronizationEvent,
        FALSE
    );
    CreateWorkerThread(this);
#endif // WORTK_THREAD_MODE


    return ntStatus;
} // Initialize

//=============================================================================
NTSTATUS
CSaveData::InitializeWorkItems
(
    IN  PDEVICE_OBJECT          DeviceObject
)
{
    PAGED_CODE();

    ASSERT(DeviceObject);

    NTSTATUS                    ntStatus = STATUS_SUCCESS;

    DPF_ENTER(("[CSaveData::InitializeWorkItems]"));

    if (m_pWorkItems)
    {
        return ntStatus;
    }

    m_pWorkItems = (PSAVEWORKER_PARAM)
        ExAllocatePoolWithTag
        (
            NonPagedPool,
            sizeof(SAVEWORKER_PARAM) * MAX_WORKER_ITEM_COUNT,
            MSVAD_POOLTAG
        );
    if (m_pWorkItems)
    {
        for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++)
        {

            m_pWorkItems[i].WorkItem = IoAllocateWorkItem(DeviceObject);
            if(m_pWorkItems[i].WorkItem == NULL)
            {
              return STATUS_INSUFFICIENT_RESOURCES;
            }
            KeInitializeEvent
            (
                &m_pWorkItems[i].EventDone,
                NotificationEvent,
                TRUE
            );
        }
    }
    else
    {
        ntStatus = STATUS_INSUFFICIENT_RESOURCES;
    }

    return ntStatus;
} // InitializeWorkItems

//=============================================================================

IO_WORKITEM_ROUTINE SaveFrameWorkerCallback, ReceiveDataWorkerCallback;

VOID
SaveFrameWorkerCallback
(
    PDEVICE_OBJECT pDeviceObject, IN  PVOID  Context
)
{
    UNREFERENCED_PARAMETER(pDeviceObject);

    PAGED_CODE();

    ASSERT(Context);

    PSAVEWORKER_PARAM           pParam = (PSAVEWORKER_PARAM) Context;
    PCSaveData                  pSaveData;

    if (NULL == pParam)
    {
        // This is completely unexpected, assert here.
        //
        ASSERT(pParam);
        return;
    }

    // DPF(D_VERBOSE, ("[SaveFrameWorkerCallback], %d", pParam->ulFrameNo));
    DPF_ENTER(("[SaveFrameWorkerCallback], %d", pParam->ulFrameNo));

    ASSERT(pParam->pSaveData);
    ASSERT(pParam->pSaveData->m_fFrameUsed);

#if 1
    if (pParam->WorkItem) {
        pSaveData = pParam->pSaveData;

        if (pSaveData) {
            pSaveData->SendData(pParam->bForceFlush);
        }
    }

#else
    if (pParam->WorkItem)
    {
        pSaveData = pParam->pSaveData;

        if (STATUS_SUCCESS == KeWaitForSingleObject
            (
                &pSaveData->m_FileSync,
                Executive,
                KernelMode,
                FALSE,
                NULL
            ))
        {
            if (NT_SUCCESS(pSaveData->FileOpen(FALSE)))
            { 
                pSaveData->FileWrite(pParam->pData, pParam->ulDataSize);
                // pSaveData->FileClose();
            }
            InterlockedExchange( (LONG *)&(pSaveData->m_fFrameUsed[pParam->ulFrameNo]), FALSE );

            KeReleaseMutex( &pSaveData->m_FileSync, FALSE );
        }
    }

#endif

    KeSetEvent(&pParam->EventDone, 0, FALSE);
} // SaveFrameWorkerCallback

VOID
ReceiveDataWorkerCallback
(
    PDEVICE_OBJECT pDeviceObject, IN  PVOID  Context
)
{
    UNREFERENCED_PARAMETER(pDeviceObject);

    PAGED_CODE();

    ASSERT(Context);

    PSAVEWORKER_PARAM           pParam = (PSAVEWORKER_PARAM)Context;
    PCSaveData                  pSaveData;

    if (NULL == pParam)
    {
        // This is completely unexpected, assert here.
        //
        ASSERT(pParam);
        return;
    }

    if (pParam->WorkItem) {
        pSaveData = pParam->pSaveData;

        if (NT_SUCCESS(pSaveData->FileOpen(FALSE))) {
            pSaveData->FileRead(pParam->pData, pParam->ulDataSize);
            // pSaveData->FileClose();
        }
    }

    KeSetEvent(&pParam->EventDone, 0, FALSE);
} // ReceiveDataWorkerCallback

//=============================================================================
NTSTATUS
CSaveData::SetDataFormat
(
    IN PKSDATAFORMAT            pDataFormat
)
{
    PAGED_CODE();
    NTSTATUS                    ntStatus = STATUS_SUCCESS;
 
    DPF_ENTER(("[CSaveData::SetDataFormat]"));

    ASSERT(pDataFormat);

    PWAVEFORMATEX pwfx = NULL;

    if (IsEqualGUIDAligned(pDataFormat->Specifier,
        KSDATAFORMAT_SPECIFIER_DSOUND))
    {
        pwfx =
            &(((PKSDATAFORMAT_DSOUND) pDataFormat)->BufferDesc.WaveFormatEx);
    }
    else if (IsEqualGUIDAligned(pDataFormat->Specifier,
        KSDATAFORMAT_SPECIFIER_WAVEFORMATEX))
    {
        pwfx = &((PKSDATAFORMAT_WAVEFORMATEX) pDataFormat)->WaveFormatEx;
    }

    if (pwfx)
    {
        // Free the previously allocated waveformat
        if (m_waveFormat)
        {
            ExFreePoolWithTag(m_waveFormat, MSVAD_POOLTAG);
        }

        m_waveFormat = (PWAVEFORMATEX)
            ExAllocatePoolWithTag
            (
                NonPagedPool,
                (pwfx->wFormatTag == WAVE_FORMAT_PCM) ?
                sizeof( PCMWAVEFORMAT ) :
                sizeof( WAVEFORMATEX ) + pwfx->cbSize,
                MSVAD_POOLTAG
            );

        if(m_waveFormat)
        {
            RtlCopyMemory( m_waveFormat,
                           pwfx,
                           (pwfx->wFormatTag == WAVE_FORMAT_PCM) ?
                           sizeof( PCMWAVEFORMAT ) :
                           sizeof( WAVEFORMATEX ) + pwfx->cbSize);

            // m_ulTransferChunkSize = pwfx->nAvgBytesPerSec / 100 * 2;
        }
        else
        {
            ntStatus = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    INT settedCnt = 0;

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->SetDataFormat(m_waveFormat);

            if (!NT_SUCCESS(ntStatus)) {
                continue;
            }

            settedCnt += 1;
        }
    }

    ntStatus = (settedCnt > 0) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

    return ntStatus;
} // SetDataFormat

//=============================================================================
void
CSaveData::ReadData
(
    _Inout_updates_bytes_all_(ulByteCount)  PBYTE   pBuffer,
    _In_                                    ULONG   ulByteCount
)
{
    //NTSTATUS          ntStatus = STATUS_SUCCESS;
    //LARGE_INTEGER     timeOut;
    //PSAVEWORKER_PARAM pParam;

    UNREFERENCED_PARAMETER(pBuffer);

    DPF_ENTER(("[CSaveData::ReadData] ulByteCount=%lu", ulByteCount));

    if (0 == ulByteCount) {
        return;
    }

#ifdef  READ_DIRECT
    // FileRead(pBuffer, ulByteCount);
#else
    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer;
    UINT32 uiChunk, uiStart, uiCopied = 0;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    i64ToTransfer = i64Wpos - i64Rpos;
    if (i64ToTransfer < 0) {
        i64ToTransfer += m_ulBufferSize;
    }
    i64ToTransfer = min(i64ToTransfer, ulByteCount);

    while (i64ToTransfer) {
        uiStart = (UINT32)(i64Rpos & m_ulBufferSizeMask);
        uiChunk = (UINT32)min(i64ToTransfer, m_ulBufferSize - uiStart);

        RtlCopyMemory(pBuffer + uiCopied, m_pDataBuffer + uiStart, uiChunk);

        uiCopied += uiChunk;
        i64Rpos += uiChunk;
        i64ToTransfer -= uiChunk;
    }

    InterlockedExchange((LONG *)&m_i64Rpos, (LONG)(i64Rpos & m_ulBufferSizeMask));

    // RtlZeroMemory(pBuffer + uiCopied, ulByteCount - uiCopied);
#endif //  READ_DIRECT

#if 0
    timeOut.QuadPart = -5 * 1000 * 1000;

    pParam = &m_pWorkItems[WORKITEM_TYPE_RECORD]; // GetNewWorkItem(WORKITEM_TYPE_RECORD);
    if (pParam == NULL) {
        return;
    }

    ntStatus =
        KeWaitForSingleObject
        (
            &pParam->EventDone,
            Executive,
            KernelMode,
            FALSE,
            &timeOut
        );

    if (ntStatus == STATUS_SUCCESS) {
        // ReceiveData(pBuffer, ulByteCount);

        pParam->pSaveData = this;
        pParam->pData = pBuffer;
        pParam->ulDataSize = ulByteCount;

        KeResetEvent(&pParam->EventDone);

        IoQueueWorkItem(pParam->WorkItem, ReceiveDataWorkerCallback,
            CriticalWorkQueue, (PVOID)pParam);

        timeOut.QuadPart = -5 * 1000 * 1000;

        ntStatus =
            KeWaitForSingleObject
            (
                &pParam->EventDone,
                Executive,
                KernelMode,
                FALSE,
                &timeOut
            );
    }
#endif
} // ReadData

//=============================================================================
#pragma code_seg()
void
CSaveData::SaveFrame
(
    IN ULONG                    ulFrameNo,
    IN ULONG                    ulDataSize
)
{
    PSAVEWORKER_PARAM           pParam = NULL;

    DPF_ENTER(("[CSaveData::SaveFrame]"));

    pParam = GetNewWorkItem(WORKITEM_TYPE_PLAYBACK);
    if (pParam)
    {
        DPF_ENTER(("[CSaveData::SaveFrame] get work item %u", ulFrameNo));

        pParam->pSaveData = this;
        pParam->ulFrameNo = ulFrameNo;
        pParam->ulDataSize = ulDataSize;
        pParam->pData = m_pDataBuffer + ulFrameNo * m_ulFrameSize;
        KeResetEvent(&pParam->EventDone);
        IoQueueWorkItem(pParam->WorkItem, SaveFrameWorkerCallback,
                        CriticalWorkQueue, (PVOID)pParam);
    } else {
        DPF_ENTER(("[CSaveData::SaveFrame] Failed to get work item for %u", ulFrameNo));
    }
} // SaveFrame

UINT32 CSaveData::BufferAvailSize()
{
    INT64 iAvail;
    INT64 i64Wpos, i64Rpos;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    iAvail = i64Wpos - i64Rpos;
    if (iAvail < 0)
        iAvail += m_ulBufferSize;

    return UINT32(iAvail);
}

UINT32 CSaveData::BufferFreeSize()
{
    INT64 iFree;
    INT64 i64Wpos, i64Rpos;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    iFree = i64Rpos - i64Wpos;
    if (iFree <= 0)
        iFree += m_ulBufferSize;

    return UINT32(iFree);
}

#pragma code_seg("PAGE")
//=============================================================================
void
CSaveData::WaitAllWorkItems
(
    void
)
{
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::WaitAllWorkItems]"));

    // Save the last partially-filled frame
    // SaveFrame(m_ulFramePtr, m_ulBufferPtr - (m_ulFramePtr * m_ulFrameSize));
    SendFlushEvent(TRUE);

    // may be not call initial...
    if (!m_pWorkItems) {
        return;
    }

    for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++)
    {
        DPF(D_VERBOSE, ("[Waiting for WorkItem] %d", i));
        KeWaitForSingleObject
        (
            &(m_pWorkItems[i].EventDone),
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
    }
} // WaitAllWorkItems

#pragma code_seg()
//=============================================================================
void
CSaveData::WriteData
(
    _In_reads_bytes_(ulByteCount)   PBYTE   pBuffer,
    _In_                            ULONG   ulByteCount
)
{
    ASSERT(pBuffer);

    BOOL                        fSaveFrame = FALSE;
    ULONG                       ulSaveFramePtr = 0;

    // If stream writing is disabled, then exit.
    //
    if (m_fWriteDisabled)
    {
        return;
    }

    DPF_ENTER(("[CSaveData::WriteData ulByteCount=%lu]", ulByteCount));

    if( 0 == ulByteCount )
    {
        return;
    }

    DbgLatencyStart();

#if 0
    UNREFERENCED_PARAMETER(fSaveFrame);
    UNREFERENCED_PARAMETER(ulSaveFramePtr);

    UINT32 uiWritten;
    PSAVEWORKER_PARAM           pParam = NULL;

    uiWritten = ring_buffer_write(&m_rbuf, pBuffer, ulByteCount); // _locked
    if (ulByteCount != uiWritten) {
        DPF_ENTER(("[CSaveData::WriteData uiWritten=%d]", uiWritten));
    }

    pParam = GetNewWorkItem();
    if (pParam) {

        pParam->pSaveData = this;

        KeResetEvent(&pParam->EventDone);

        IoQueueWorkItem(pParam->WorkItem, SaveFrameWorkerCallback,
            CriticalWorkQueue, (PVOID)pParam);
    }

#elif 1
    UNREFERENCED_PARAMETER(fSaveFrame);
    UNREFERENCED_PARAMETER(ulSaveFramePtr);

    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer, i64Copied = 0;
    INT32 uiStart, uiChunk;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    i64ToTransfer = i64Rpos - i64Wpos;
    if (i64ToTransfer <= 0) {
        i64ToTransfer += m_ulBufferSize;
    }

    i64ToTransfer = min(i64ToTransfer, ulByteCount);

    if (!i64ToTransfer) {
        DPF_ENTER(("[CSaveData::WriteData OverRun]"));

        // !!! TODO !!!
        // try flush ring buffer
    }

    while (i64ToTransfer) {
        uiStart = (INT32)(i64Wpos & m_ulBufferSizeMask);
        uiChunk = (INT32)(min(i64ToTransfer, m_ulBufferSize - uiStart));

        RtlCopyMemory(m_pDataBuffer + uiStart, pBuffer + i64Copied, uiChunk);

        i64Copied     += uiChunk;
        i64Wpos       += uiChunk;
        i64ToTransfer -= uiChunk;
    }

    // InterlockedAdd((LONG *)&m_i64Wpos, (LONG)i64Copied);
    InterlockedExchange((LONG *)&m_i64Wpos, (LONG)(i64Wpos & m_ulBufferSizeMask));

#ifdef WORTK_THREAD_MODE
    UNREFERENCED_PARAMETER(pParam);

    KeSetEvent(&this->m_WakeUpWorker, EVENT_INCREMENT, FALSE);
#else

    SendFlushEvent(FALSE/*i64Copied < m_ulTransferChunkSize*/);

#endif // WORTK_THREAD_MODE

#else
    // Check to see if this frame is available.
    KeAcquireSpinLockAtDpcLevel( &m_FrameInUseSpinLock );
    if (!m_fFrameUsed[m_ulFramePtr])
    {
        KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );

        ULONG ulWriteBytes = ulByteCount;

        if( (m_ulBufferSize - m_ulBufferPtr) < ulWriteBytes )
        {
            ulWriteBytes = m_ulBufferSize - m_ulBufferPtr;
        }

        RtlCopyMemory(m_pDataBuffer + m_ulBufferPtr, pBuffer, ulWriteBytes);
        m_ulBufferPtr += ulWriteBytes;

        // Check to see if we need to save this frame
        if (m_ulBufferPtr >= ((m_ulFramePtr + 1) * m_ulFrameSize))
        {
            fSaveFrame = TRUE;
        }

        // Loop the buffer, if we reached the end.
        if (m_ulBufferPtr == m_ulBufferSize)
        {
            fSaveFrame = TRUE;
            m_ulBufferPtr = 0;
        }

        if (fSaveFrame)
        {
            InterlockedExchange( (LONG *)&(m_fFrameUsed[m_ulFramePtr]), TRUE );
            ulSaveFramePtr = m_ulFramePtr;
            m_ulFramePtr = (m_ulFramePtr + 1) % m_ulFrameCount;
        }

        // Write the left over if the next frame is available.
        if (ulWriteBytes != ulByteCount)
        {
            KeAcquireSpinLockAtDpcLevel( &m_FrameInUseSpinLock );
            if (!m_fFrameUsed[m_ulFramePtr])
            {
                KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
                RtlCopyMemory
                (
                    m_pDataBuffer + m_ulBufferPtr,
                    pBuffer + ulWriteBytes,
                    ulByteCount - ulWriteBytes
                );
                 m_ulBufferPtr += ulByteCount - ulWriteBytes;
            }
            else
            {
                KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
                DPF(D_BLAB, ("[Frame overflow, next frame is in use]"));
            }
        }

        if (fSaveFrame)
        {
            SaveFrame(ulSaveFramePtr, m_ulFrameSize);
        }
    }
    else
    {
        KeReleaseSpinLockFromDpcLevel( &m_FrameInUseSpinLock );
        DPF(D_BLAB, ("[Frame %d is in use]", m_ulFramePtr));
        DPF_ENTER(("[Frame %d is in use]", m_ulFramePtr));
    }
#endif

    DbgLatencyEnd(beginTime, endTime, "[CSaveData::WriteData] write ring buffer latency");

#ifdef LATENCY_DEBUG
    static LARGE_INTEGER lastSendTime = { 0 };

    DbgLatencyEnd(lastSendTime, endTime, "[CSaveData::WriteData] write ring buffer since last send");

    lastSendTime = endTime;
#endif // DEBUG_LATENCY

} // WriteData

int consume_rbuf_by_vio(uint8_t* buf, size_t len, void *other)
{
    PCSaveData saveData = (PCSaveData)other;

    if (saveData) {
        if (NT_SUCCESS(saveData->FileOpen(FALSE))) {

            saveData->FileWrite(buf, len);
        }
    }

    return 0;
}

void CSaveData::SendData(BOOL bForceFlush)
{

    DPF_ENTER(("[CSaveData::SendData]"));

#if 0
    ring_buffer_lock(&m_rbuf);

    UINT32 avail = ring_buffer_avail(&m_rbuf);

#if 1
    if (avail >= 1 * m_ulTransferChunkSize)
    {
        ring_buffer_consume(&m_rbuf, avail, consume_rbuf_by_vio, this);
    }
#elif 0
    UINT32 transfer = avail;

    while (transfer) {
        UINT32 eate = min(transfer, m_ulTransferChunkSize);
        UINT32 avail = ring_buffer_consume(&m_rbuf, eate, consume_rbuf_by_vio, this);

        transfer -= avail;
    }
#endif

    ring_buffer_unlock(&m_rbuf);

    DPF_ENTER(("[CSaveData::SendData consume %u]", avail));
#endif

#if 1
    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer, i64Copied = 0;
    UINT32 uiChunk, uiStart;
    static LONG lFlushTolerance = 0;

#define TRY_FLUSH_MAX_LIMIT (2)


    DbgLatencyStart();

    int iLooCount = 0;
    BOOL bStop = FALSE;

    while (!bStop) {

        i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
        i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

        i64Copied = 0;
        i64ToTransfer = i64Wpos - i64Rpos;
        if (i64ToTransfer < 0) {
            i64ToTransfer += m_ulBufferSize;
        }

        if (!i64ToTransfer) {
            break;
        }

        ++iLooCount;

        while (i64ToTransfer) {
            uiStart = (UINT32)(i64Rpos & m_ulBufferSizeMask);
            uiChunk = (UINT32)min(i64ToTransfer, m_ulBufferSize - uiStart);
            uiChunk = (UINT32)min(m_ulTransferChunkSize, uiChunk);

            DPF_ENTER(("[CSaveData::SendData loop ing i64Wpos %lld i64Rpos %lld, i64ToTransfer %lld, loop count %d, force flush %d, process %u, flush tolerance %ld]",
                i64Wpos, i64Rpos, i64ToTransfer, iLooCount, bForceFlush, uiChunk, lFlushTolerance));

            if (m_ulTransferChunkSize != uiChunk && !bForceFlush) {
                if ((lFlushTolerance <= TRY_FLUSH_MAX_LIMIT)) {
                    ++lFlushTolerance;
                    bStop = TRUE;
                    break;
                }
            }
            else {
                lFlushTolerance = 0;
            }

            if (NT_SUCCESS(this->FileOpen(FALSE))) {

                this->FileWrite(m_pDataBuffer + uiStart, uiChunk);
            }

            i64Copied     += uiChunk;
            i64Rpos       += uiChunk;
            i64ToTransfer -= uiChunk;
        }

        // InterlockedAdd((LONG *)&m_i64Rpos, (LONG)i64Copied);
        InterlockedExchange((LONG *)&m_i64Rpos, (LONG)(i64Rpos & m_ulBufferSizeMask));

        if (i64Copied && !bStop) {
            lFlushTolerance = 0;
        }
    }

    DbgLatencyEnd(beginTime, endTime,"[CSaveData::SendData FileWrite] latency");

#ifdef LATENCY_DEBUG
    static LARGE_INTEGER lastSendTime = { 0 };

    DbgLatencyEnd(lastSendTime, endTime, "[CSaveData::SendData FileWrite] since last send");

    lastSendTime = endTime;
#endif // DEBUG_LATENCY

#endif

    DPF_ENTER(("[CSaveData::SendData end]"));
}

void CSaveData::ReceiveData(
    _Inout_updates_bytes_all_(ulByteCount)  PBYTE   pBuffer,
    _In_                                    ULONG   ulByteCount
)
{
    PSAVEWORKER_PARAM pParam = NULL;

    DPF_ENTER(("[CSaveData::ReceiveData] ulByteCount=%lu", ulByteCount));

    pParam = GetNewWorkItem(WORKITEM_TYPE_RECORD);
    if (pParam) {

        pParam->pSaveData = this;
        pParam->pData = pBuffer;
        pParam->ulDataSize = ulByteCount;

        KeResetEvent(&pParam->EventDone);

        IoQueueWorkItem(pParam->WorkItem, ReceiveDataWorkerCallback,
            CriticalWorkQueue, (PVOID)pParam);
    }

}

BOOL CSaveData::SendFlushEvent(BOOL bForceFlush)
{
    PSAVEWORKER_PARAM pParam = NULL;

    DPF_ENTER(("[CSaveData::SendFlushEvent]"));

    pParam = GetNewWorkItem(WORKITEM_TYPE_PLAYBACK);
    if (pParam) {

        pParam->pSaveData = this;
        pParam->bForceFlush = bForceFlush;

        KeResetEvent(&pParam->EventDone);

        IoQueueWorkItem(pParam->WorkItem, SaveFrameWorkerCallback,
            CriticalWorkQueue, (PVOID)pParam);
    }

    return (pParam != NULL);
}