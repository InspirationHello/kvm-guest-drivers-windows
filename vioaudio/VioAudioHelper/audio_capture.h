#ifndef AUDIO_CAPTRUE_H_
#define AUDIO_CAPTRUE_H_

#include "precomp.h"

#include <Windows.h>
#include <Dshow.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <comdef.h>
#include <functional>


struct AudioSink {
    virtual ~AudioSink()
    {

    }

    virtual BOOL Open(CONST CHAR* Path)
    {
        return TRUE;
    }

    virtual HRESULT SetFormat(const WAVEFORMATEX *pwfx)
    {
        m_format = *pwfx;

        DebugFormateInfo(pwfx);

        return S_OK;
    }

    static VOID DebugFormateInfo(const WAVEFORMATEX *pwfx)
    {
        WarnMessage(
            "wFormatTag: %d, "
            "nChannels: %d, "
            "nSamplesPerSec: %ld, "
            "wBitsPerSample: %d\n", 
            pwfx->wFormatTag, 
            pwfx->nChannels,
            pwfx->nSamplesPerSec,
            pwfx->wBitsPerSample
        );
    }

    virtual  HRESULT CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone)
    {
        return S_OK;
    }

    WAVEFORMATEX m_format;
};


typedef DWORD(*ProcessDataPfn)(PVOID pDest, PVOID pSrc, DWORD dwSize);

struct AudioAioSink : public AudioSink {

    typedef std::function<DWORD(PVOID, PVOID, DWORD)> Processcallable;

    AudioAioSink(CDevice *device);
    ~AudioAioSink();

    virtual BOOL Init();
    BOOL Open(CONST CHAR* Path);
    virtual DWORD ProcessData(Processcallable callback, INT32 PeriodSize, PVOID pDest, DWORD dwSize);
    DWORD WriteToFileInner(PVOID pDest, PVOID pSrc, DWORD dwSize);
    virtual DWORD FlushToFile();
    HRESULT CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone);
    BOOL WaitForWoker(DWORD dwMilliseconds);

    static DWORD WINAPI WokerRoutine(
        LPVOID lpThreadParameter
    );



    CDevice *m_device           = NULL;
    UINT8   *m_pBuffer          = NULL; // m_pBuffer[8192 * 2] = { 0 };
    ULONG    m_ulBufferSize     = 0; /* must be pow of two */
    ULONG    m_ulBufferSizeMask = 0;
    INT64    m_i64Wpos = 0, m_i64Rpos = 0;

    HANDLE   m_hFile            = NULL;
    HANDLE   m_hThread          = NULL;
    HANDLE   m_hDoneEvent       = NULL;
    HANDLE   m_hNotifyEvent     = NULL;
};

/* !!! Not thread safe, should use metux or spinlock...... !!! */
struct  AudioAioTcpSink : public AudioAioSink {
    AudioAioTcpSink(CDevice *device, int port);
    ~AudioAioTcpSink();

    virtual BOOL Init();

    int Listen();
    int Accept();
    int Shutdown();
    int Close();

    DWORD SendToClients(PVOID pDest, PVOID pSrc, DWORD dwSize);
    DWORD SendBoth(PVOID pDest, PVOID pSrc, DWORD dwSize);
    DWORD FlushToFile();

    int sendn(int fd, const void *vptr, size_t n);

#define DEFAULT_PORT "6666"
    SOCKET              listenSocket = INVALID_SOCKET;
    std::vector<SOCKET> clientSockes;
    BOOL                wsaInited    = FALSE;
    int                 port         = 6666;

    static DWORD WINAPI AcceptRoutine(
        LPVOID lpThreadParameter
    );

    HANDLE              hAccepterThread = NULL;
    volatile BOOL       bDone           = FALSE;
};

HRESULT RecordAudioStream(AudioSink *pAudioSink);

#endif // !AUDIO_CAPTRUE_H_
