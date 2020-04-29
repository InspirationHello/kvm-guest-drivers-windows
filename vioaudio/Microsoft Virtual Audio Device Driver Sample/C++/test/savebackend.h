#ifndef _MSVAD_SAVEBACKEND_H
#define _MSVAD_SAVEBACKEND_H

//-----------------------------------------------------------------------------
//  Class
//-----------------------------------------------------------------------------

class CSaveBackend
{
protected:
    HANDLE                      m_FileHandle;       // DataFile handle.
    UNICODE_STRING              m_FileName;         // DataFile name.
    OBJECT_ATTRIBUTES           m_objectAttributes; // Used for opening file.
    PLARGE_INTEGER              m_pFilePtr;

    PWAVEFORMATEX               m_waveFormat;

public:
    CSaveBackend();
    virtual ~CSaveBackend();

    virtual void                Disable
    (
        BOOL                    fDisable
    );
    virtual NTSTATUS            Initialize
    (
        ULONG                   StreamId
    );
    virtual NTSTATUS            SetDataFormat
    (
        IN  PWAVEFORMATEX       pWaveFormat
    );

public:
    virtual NTSTATUS            FileClose
    (
        void
    );
    virtual NTSTATUS            FileOpen
    (
        IN  BOOL                fOverWrite
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
typedef CSaveBackend *PCSaveBackend;

#endif
