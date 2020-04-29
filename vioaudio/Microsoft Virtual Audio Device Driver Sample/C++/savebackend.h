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

    LONG                        m_Volume;

    UNICODE_STRING              m_RegistryPath;

public:
    CSaveBackend();
    CSaveBackend(_In_ PUNICODE_STRING RegistryPath);
    virtual ~CSaveBackend();

    virtual void                Disable
    (
        BOOL                    fDisable
    );
    virtual NTSTATUS            Initialize
    (
        ULONG                   StreamId,
        BOOL                    fCaptrue
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
    virtual LONG                GetVolume
    (
        _In_  LONG              Channel
    );
    virtual void                SetMute
    (
        _In_  BOOL              Value
    );
    virtual BOOL                GetMute
    (
    );


    static NTSTATUS             GetSettingsRegistryPath(
        _In_                    PUNICODE_STRING RegistryPath,
        _Inout_                 PUNICODE_STRING SettingsPath
    );
    static NTSTATUS             SaveSettings(
        _In_                    PUNICODE_STRING RegistryPath,
        _In_                    PCWSTR          ValueName,
        _In_                    ULONG           ValueType,
        _In_                    PVOID           ValueData,
        _In_                    ULONG           ValueLength
    );
    static NTSTATUS             QuerySettings(
        _In_                    PUNICODE_STRING RegistryPath,
        _In_                    PRTL_QUERY_REGISTRY_TABLE RegistryTable,
        _In_                    UINT Size
    );
    static NTSTATUS             DeleteSettings(
        _In_                    PUNICODE_STRING RegistryPath,
        _In_                    PCWSTR ValueName
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
    virtual DWORD               FileRead
    (
        _Out_writes_bytes_(ulBufferSize)  PBYTE   pBuffer,
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
typedef CSaveBackend *PCSaveBackend;

#endif
