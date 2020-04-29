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

#define DEFAULT_FRAME_COUNT         (64  * 4 * 2 * 2 * 40)
#define DEFAULT_FRAME_SIZE          (1920) // (1764 1920 * 10)
#define DEFAULT_BUFFER_SIZE         (DEFAULT_FRAME_SIZE * DEFAULT_FRAME_COUNT)

#define DEFAULT_FILE_NAME           L"\\DosDevices\\C:\\STREAM"

#define MAX_WORKER_ITEM_COUNT       1

//=============================================================================
// Statics
//=============================================================================
ULONG CSaveData::m_ulStreamId = 0;


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
    if (roundup_pow_of_two(len) != len) {
        // assert(0);
        return -1;
    }

    rbuf->rpos = rbuf->wpos = 0;
    rbuf->data = data;
    rbuf->size = len;
    rbuf->size_mask = rbuf->size - 1;

    ring_buffer_lock_init(rbuf);

    return 0;
}

uint32_t ring_buffer_free(ring_buffer_t *rbuf)
{
    int32_t free;

    free = rbuf->rpos - rbuf->wpos;
    if (free <= 0)
        free += rbuf->size;

    return (uint32_t)(free - 1);
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
        copied += chunk;
        rpos += chunk;
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

    m_ulStreamId++;
    InitializeWorkItems(GetDeviceObject());
} // CSaveData

//=============================================================================
CSaveData::~CSaveData()
{
    PAGED_CODE();

    DPF_ENTER(("[CSaveData::~CSaveData]"));

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

    DestroyWorkItems();

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
                DPF(D_TERSE, ("[CSaveData::m_SaveBackends %d FileClose Error: 0x%08x]", i, ntStatus));
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
    PAGED_CODE();

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

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileOpen(fOverWrite);

            if (!NT_SUCCESS(ntStatus)) {
                DPF(D_TERSE, ("[CSaveData::m_SaveBackends %d FileOpen Error: 0x%08x]", i, ntStatus));
                break;
            }
        }
    }

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

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->FileWrite(pData, ulDataSize);

            if (!NT_SUCCESS(ntStatus)) {
                DPF(D_TERSE, ("[CSaveData::m_SaveBackends %d WriteFile Error: 0x%08x]", i, ntStatus));
                break;
            }
        }
    }

    return ntStatus;
} // FileWrite

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
    void
)
{
    LARGE_INTEGER               timeOut = { 0 };
    NTSTATUS                    ntStatus;

    DPF_ENTER(("[CSaveData::GetNewWorkItem]"));

    for (int i = 0; i < MAX_WORKER_ITEM_COUNT; i++)
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
    void
)
{
    PAGED_CODE();

    NTSTATUS    ntStatus = STATUS_SUCCESS;
    WCHAR       szTemp[MAX_PATH];
    size_t      cLen;

    DPF_ENTER(("[CSaveData::Initialize]"));

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

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->Initialize(m_ulStreamId);

            if (!NT_SUCCESS(ntStatus)) {
                break;
            }
        }
    }

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

IO_WORKITEM_ROUTINE SaveFrameWorkerCallback;

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
            pSaveData->SendData();
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

    for (int i = 0; i < m_SaveBackendsLength; i++) {

        if (m_SaveBackends[i] != NULL) {
            ntStatus = m_SaveBackends[i]->SetDataFormat(m_waveFormat);

            if (!NT_SUCCESS(ntStatus)) {
                break;
            }
        }
    }

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
    UNREFERENCED_PARAMETER(pBuffer);
    UNREFERENCED_PARAMETER(ulByteCount);

    PAGED_CODE();

    // Not implemented yet.
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

    pParam = GetNewWorkItem();
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
    SaveFrame(m_ulFramePtr, m_ulBufferPtr - (m_ulFramePtr * m_ulFrameSize));

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

#if 1
    UNREFERENCED_PARAMETER(fSaveFrame);
    UNREFERENCED_PARAMETER(ulSaveFramePtr);

    UINT32 uiWritten;
    PSAVEWORKER_PARAM           pParam = NULL;

    uiWritten = ring_buffer_write_locked(&m_rbuf, pBuffer, ulByteCount);
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

#elif 0
    UNREFERENCED_PARAMETER(fSaveFrame);
    UNREFERENCED_PARAMETER(ulSaveFramePtr);

    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer, i64Copied = 0;
    INT32 uiStart, uiChunk;
    PSAVEWORKER_PARAM           pParam = NULL;

#ifdef TRY_USE_SPINLOCK
    KeAcquireSpinLockAtDpcLevel(&m_FrameInUseSpinLock);
    i64Wpos = m_i64Wpos;
    i64Rpos = m_i64Rpos;
    KeReleaseSpinLockFromDpcLevel(&m_FrameInUseSpinLock);
#else
    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);
#endif

    i64ToTransfer = min(m_ulBufferSize - (i64Wpos - i64Rpos), ulByteCount);

    if (!i64ToTransfer) {
        // !!! TODO !!!
        DPF_ENTER(("[CSaveData::WriteData OverRun]"));
    }

    while (i64ToTransfer) {
        uiStart = (INT32)(i64Wpos & m_ulBufferSizeMask);
        uiChunk = (INT32)(min(i64ToTransfer, m_ulBufferSize - uiStart));

        RtlCopyMemory(m_pDataBuffer + uiStart, pBuffer + i64Copied, uiChunk);

        i64Copied     += uiChunk;
        i64Wpos       += uiChunk;
        i64ToTransfer -= uiChunk;
    }

#ifdef TRY_USE_SPINLOCK
    KeAcquireSpinLockAtDpcLevel(&m_FrameInUseSpinLock);
    m_i64Wpos += i64Copied; // (i64Wpos & m_ulBufferSizeMask);
    KeReleaseSpinLockFromDpcLevel(&m_FrameInUseSpinLock);
#else
    InterlockedAdd((LONG *)&m_i64Wpos, (LONG)i64Copied);
#endif


    DPF_ENTER(("[CSaveData::SaveFrame]"));

    pParam = GetNewWorkItem();
    if (pParam) {

        pParam->pSaveData = this;

        KeResetEvent(&pParam->EventDone);
        
        IoQueueWorkItem(pParam->WorkItem, SaveFrameWorkerCallback,
            CriticalWorkQueue, (PVOID)pParam);
    }

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

void CSaveData::SendData()
{

    DPF_ENTER(("[CSaveData::SendData]"));

    ring_buffer_lock(&m_rbuf);

    UINT32 avail = ring_buffer_consume(&m_rbuf, m_ulTransferChunkSize, consume_rbuf_by_vio, this);

    ring_buffer_unlock(&m_rbuf);

    DPF_ENTER(("[CSaveData::SendData consume %u]", avail));

#if 0
    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTranfer, i64Copied = 0;
    UINT32 uiChunk, uiStart;


    LARGE_INTEGER begin = { 0 };
    KeQuerySystemTime(&begin);

    int loo_count = 0;

    // while (TRUE) {

#ifdef TRY_USE_SPINLOCK
        KIRQL oldIrql;

        KeAcquireSpinLock(&m_FrameInUseSpinLock, &oldIrql);
        if (m_i64Wpos - m_i64Rpos == m_ulBufferSize) {
            m_i64Rpos = 0;
            m_i64Wpos = 0;
            // Overrun ...
            DPF_ENTER(("[CSaveData::SendData Overrun]"));
}
        i64Wpos = m_i64Wpos;
        i64Rpos = m_i64Rpos;
        KeReleaseSpinLock(&m_FrameInUseSpinLock, oldIrql);
#else
        i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
        i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);
#endif

        DPF_ENTER(("[CSaveData::SendData loop ing %lld %lld, loop count %d]", 
            i64Wpos, i64Rpos, ++loo_count));

        i64Copied = 0;
        i64ToTranfer = i64Wpos - i64Rpos;

        if (i64ToTranfer < m_ulTransferChunkSize) {
            // break;
        }

        while (i64ToTranfer) {
            uiStart = (UINT32)(i64Rpos & m_ulBufferSizeMask);
            uiChunk = (UINT32)min(i64ToTranfer, m_ulBufferSize - uiStart);
            uiChunk = (UINT32)min(m_ulTransferChunkSize, uiChunk);

            DPF_ENTER(("[CSaveData::SendData loop ing %lld %lld, loop count %d, process %u]",
                i64Wpos, i64Rpos, ++loo_count, uiChunk));

            if (m_ulTransferChunkSize != uiChunk) {
                break;
            }

            if (NT_SUCCESS(this->FileOpen(FALSE))) {

                this->FileWrite(m_pDataBuffer + uiStart, uiChunk);
            }

            i64Copied += uiChunk;
            i64Rpos += uiChunk;
            i64ToTranfer -= uiChunk;
        }

#ifdef TRY_USE_SPINLOCK
        KeAcquireSpinLock(&m_FrameInUseSpinLock, &oldIrql);
        m_i64Rpos += i64Copied;
        KeReleaseSpinLock(&m_FrameInUseSpinLock, oldIrql);
#else
        InterlockedAdd((LONG *)&m_i64Rpos, (LONG)i64Copied);
#endif

    // }

    LARGE_INTEGER end = { 0 };
    KeQuerySystemTime(&end);

    LONGLONG Duration;

    // 100ns -> 100 / 1000000 -> 1 / 10000
    Duration = (end.QuadPart - begin.QuadPart) / (10000);

    if (Duration > 1) {
        DPF_ENTER(("[FileWrite elapsed %lld ms]", Duration));
    }
#endif

    DPF_ENTER(("[CSaveData::SendData end]"));
}


