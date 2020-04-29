#ifndef _MSVAD_TCP_SAVEDATA_H
#define _MSVAD_TCP_SAVEDATA_H

#include "savebackend.h"
#include "wsksmple.h"

//-----------------------------------------------------------------------------
//  Forward declaration
//-----------------------------------------------------------------------------
class CTcpSaveData;
typedef CTcpSaveData *PCTcpSaveData;

class CTcpSaveData : public CSaveBackend {
protected:
    PWskSampleServer            m_Server;
    PMDL                        m_BufferMdl;
    PVOID                       m_Buffer;
    ULONG                       m_BufferSize;

public:
    CTcpSaveData(_In_ PUNICODE_STRING RegistryPath);
    ~CTcpSaveData();

    virtual NTSTATUS            Initialize
    (
        ULONG                   StreamId,
        BOOL                    fCaptrue
    );
    virtual void                Disable
    (
        BOOL                    fDisable
    );
    virtual NTSTATUS            SetDataFormat
    (
        IN  PWAVEFORMATEX       pWaveFormat
    );
    virtual NTSTATUS            SetState
    (
        _In_  KSSTATE           NewState
    );
    virtual void                SetVolume
    (
        _In_  LONG              Channel,
        _In_  LONG              Value
    );
    virtual void                SetMute
    (
        _In_  BOOL              Value
    );
    virtual BOOL                GetMute
    (
    );

    virtual NTSTATUS            FileClose
    (
        void
    );
    virtual NTSTATUS            FileOpen
    (
        IN  BOOL                fOverWrite
    );
    virtual DWORD               FileRead
    (
        _Out_writes_bytes_(ulBufferSize)  PBYTE pBuffer,
        _In_                            ULONG   ulBufferSize
    );
    virtual NTSTATUS            FileWrite
    (
        _In_reads_bytes_(ulDataSize)    PBYTE   pData,
        _In_                            ULONG   ulDataSize
    );
    virtual NTSTATUS            FileWriteHeader
    (
        void
    );

};

#endif