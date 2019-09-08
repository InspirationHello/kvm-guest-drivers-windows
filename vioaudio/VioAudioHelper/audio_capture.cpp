#include "audio_capture.h"

#include "precomp.h"

//-----------------------------------------------------------
// Record an audio stream from the default audio capture
// device. The RecordAudioStream function allocates a shared
// buffer big enough to hold one second of PCM audio data.
// The function uses this buffer to stream data from the
// capture device. The main loop runs every 1/2 second.
//-----------------------------------------------------------

// REFERENCE_TIME time units per second and per millisecond
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000

#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { goto Exit; }
#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }

const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
const IID IID_IAudioClient = __uuidof(IAudioClient);
const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

BOOL AdjustFormatTo16Bits(WAVEFORMATEX *pwfx)
{
    BOOL bRet(FALSE);

    if (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT){
        pwfx->wFormatTag = WAVE_FORMAT_PCM;
        pwfx->wBitsPerSample = 16;
        pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
        pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

        bRet = TRUE;
    }
    else if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
        PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);

        if (IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)){
            pEx->SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            pEx->Samples.wValidBitsPerSample = 16;
            pwfx->wBitsPerSample = 16;
            pwfx->nBlockAlign = pwfx->nChannels * pwfx->wBitsPerSample / 8;
            pwfx->nAvgBytesPerSec = pwfx->nBlockAlign * pwfx->nSamplesPerSec;

            bRet = TRUE;
        }
    }

    return bRet;
}

HRESULT RecordAudioStream(AudioSink *pAudioSink)
{
    HRESULT hr;
    REFERENCE_TIME hnsRequestedDuration = REFTIMES_PER_SEC;
    REFERENCE_TIME hnsActualDuration;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    IAudioCaptureClient *pCaptureClient = NULL;
    WAVEFORMATEX *pwfx = NULL;
    UINT32 packetLength = 0;
    BOOL bDone = FALSE;
    BYTE *pData;
    DWORD flags;

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, NULL,
        CLSCTX_ALL, IID_IMMDeviceEnumerator,
        (void**)&pEnumerator);
    EXIT_ON_ERROR(hr)

    hr = pEnumerator->GetDefaultAudioEndpoint(
        eRender/*eCapture*/, eConsole, &pDevice);
    EXIT_ON_ERROR(hr)

    hr = pDevice->Activate(
        IID_IAudioClient, CLSCTX_ALL,
        NULL, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr)

    AdjustFormatTo16Bits(pwfx);

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK/*0*/,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    EXIT_ON_ERROR(hr)

        // Get the size of the allocated buffer.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetService(
        IID_IAudioCaptureClient,
        (void**)&pCaptureClient);
    EXIT_ON_ERROR(hr)

        // Notify the audio sink which format to use.
    hr = pAudioSink->SetFormat(pwfx);
    EXIT_ON_ERROR(hr)

        // Calculate the actual duration of the allocated buffer.
    hnsActualDuration = (double)REFTIMES_PER_SEC *
        bufferFrameCount / pwfx->nSamplesPerSec;

    hr = pAudioClient->Start();  // Start recording.
    EXIT_ON_ERROR(hr)

        // Each loop fills about half of the shared buffer.
    while (bDone == FALSE)
    {
        // Sleep for half the buffer duration.
        Sleep(hnsActualDuration / REFTIMES_PER_MILLISEC / 2);

        hr = pCaptureClient->GetNextPacketSize(&packetLength);
        EXIT_ON_ERROR(hr)

            while (packetLength != 0)
            {
                BOOL bSilence = FALSE;

                // Get the available data in the shared buffer.
                hr = pCaptureClient->GetBuffer(
                    &pData,
                    &numFramesAvailable,
                    &flags, NULL, NULL);
                EXIT_ON_ERROR(hr)

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                //    pData = NULL;  // Tell CopyData to write silence.
                    bSilence = TRUE;
                }

                // Copy the available capture data to the audio sink.
                hr = pAudioSink->CopyData(
                    pData, numFramesAvailable * pwfx->nBlockAlign, bSilence, &bDone);
                EXIT_ON_ERROR(hr)

                    hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
                EXIT_ON_ERROR(hr)

                    hr = pCaptureClient->GetNextPacketSize(&packetLength);
                EXIT_ON_ERROR(hr)
            }
    }

    hr = pAudioClient->Stop();  // Stop recording.
    EXIT_ON_ERROR(hr)

Exit:
    CoTaskMemFree(pwfx);
    SAFE_RELEASE(pEnumerator)
    SAFE_RELEASE(pDevice)
    SAFE_RELEASE(pAudioClient)
    SAFE_RELEASE(pCaptureClient)

    return hr;
}


#if 0
#pragma comment(lib,"winmm.lib")

#include <Windows.h>
#include <mmsystem.h>
#include <fstream>
#include <iostream>

// record from record device...
int start_record()
{
    // Fill the WAVEFORMATEX struct to indicate the format of our recorded audio
    //   For this example we'll use "CD quality", ie:  44100 Hz, stereo, 16-bit
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;       // PCM is standard
    wfx.nChannels = 2;                      // 2 channels = stereo sound
    wfx.nSamplesPerSec = 44100;             // Samplerate.  44100 Hz
    wfx.wBitsPerSample = 16;                // 16 bit samples
    // These others are computations:
    wfx.nBlockAlign = wfx.wBitsPerSample * wfx.nChannels / 8;
    wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;


    // Open our 'waveIn' recording device
    HWAVEIN wi;
    waveInOpen(&wi,            // fill our 'wi' handle
        WAVE_MAPPER,    // use default device (easiest)
        &wfx,           // tell it our format
        NULL, NULL,      // we don't need a callback for this example
        CALLBACK_NULL | WAVE_FORMAT_DIRECT   // tell it we do not need a callback
    );

    // At this point, we have our device, now we need to give it buffers (with headers) that it can
    //  put the recorded audio somewhere
    char buffers[2][44100 * 2 * 2 / 2];    // 2 buffers, each half of a second long
    WAVEHDR headers[2] = { {},{} };           // initialize them to zeros
    for (int i = 0; i < 2; ++i)
    {
        headers[i].lpData = buffers[i];             // give it a pointer to our buffer
        headers[i].dwBufferLength = 44100 * 2 * 2 / 2;      // tell it the size of that buffer in bytes
        // the other parts of the header we don't really care about for this example, and can be left at zero

        // Prepare each header
        waveInPrepareHeader(wi, &headers[i], sizeof(headers[i]));

        // And add it to the queue
        //  Once we start recording, queued buffers will get filled with audio data
        waveInAddBuffer(wi, &headers[i], sizeof(headers[i]));
    }

    // In this example, I'm just going to dump the audio data to a binary file
    std::ofstream outfile("my_recorded_audio.bin", std::ios_base::out | std::ios_base::binary);

    // Print some simple directions to the user
    std::cout << "Now recording audio.  Press Escape to stop and exit." << std::endl;

    // start recording!
    waveInStart(wi);

    // Now that we are recording, keep polling our buffers to see if they have been filled.
    //   If they have been, dump their contents to the file and re-add them to the queue so they
    //   can get filled again, and again, and again
    while (!(GetAsyncKeyState(VK_ESCAPE) & 0x8000))  // keep looping until the user hits escape
    {
        for (auto& h : headers)      // check each header
        {
            if (h.dwFlags & WHDR_DONE)           // is this header done?
            {
                // if yes, dump it to our file
                outfile.write(h.lpData, h.dwBufferLength);

                // then re-add it to the queue
                h.dwFlags = 0;          // clear the 'done' flag
                h.dwBytesRecorded = 0;  // tell it no bytes have been recorded

                // re-add it  (I don't know why you need to prepare it again though...)
                waveInPrepareHeader(wi, &h, sizeof(h));
                waveInAddBuffer(wi, &h, sizeof(h));
            }
        }
    }

    // Once the user hits escape, stop recording, and clean up
    waveInStop(wi);
    for (auto& h : headers)
    {
        waveInUnprepareHeader(wi, &h, sizeof(h));
    }
    waveInClose(wi);

    // All done!

    return 0;
}
#endif