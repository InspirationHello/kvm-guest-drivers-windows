#ifndef AUDIO_CAPTRUE_H_
#define AUDIO_CAPTRUE_H_

#include "precomp.h"

#include <Windows.h>
#include <Dshow.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <comdef.h>


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
        PrintMessage(
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

HRESULT RecordAudioStream(AudioSink *pAudioSink);

#endif // !AUDIO_CAPTRUE_H_
