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
              if (FAILED(hres)) { WarnMessage("%s(%d) Failed with %d\n", __FILE__, __LINE__, hres); goto Exit; }
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

typedef struct AudioSinkAux {
    HANDLE hDoneEvent;
    WAVEFORMATEX *pwfx;
    IAudioCaptureClient *pCaptureClient;
    AudioSink *pAudioSink;
}AudioSinkAux;

BOOL TimerQueueRoutine(PVOID lpParam)
{
    AudioSinkAux *audioSinkAux = (AudioSinkAux*)lpParam;
    IAudioCaptureClient *pCaptureClient = audioSinkAux->pCaptureClient;
    AudioSink *pAudioSink = audioSinkAux->pAudioSink;
    HANDLE hDoneEvent = audioSinkAux->hDoneEvent;

    HRESULT hr;
    UINT32 packetLength = 0;
    UINT32 numFramesAvailable;
    BYTE *pData;
    DWORD flags;
    WAVEFORMATEX *pwfx = audioSinkAux->pwfx;
    BOOL bDone = FALSE;

    // Sleep for half the buffer duration.

    static LatencyDebug allElapsed, eventElapsed, copyElapsed;

    hr = pCaptureClient->GetNextPacketSize(&packetLength);
    EXIT_ON_ERROR(hr)

    if (packetLength != 0)
    {
        BOOL bSilence = FALSE;

        // Get the available data in the shared buffer.
        hr = pCaptureClient->GetBuffer(
            &pData,
            &numFramesAvailable,
            &flags, NULL, NULL);
        EXIT_ON_ERROR(hr)

        // WarnMessage("in timer 1 CopyData %lu\n", numFramesAvailable);

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            //    pData = NULL;  // Tell CopyData to write silence.
            bSilence = TRUE;
        }

        // Copy the available capture data to the audio sink.
        hr = pAudioSink->CopyData(
            pData, numFramesAvailable * pwfx->nBlockAlign, bSilence, &bDone);
        EXIT_ON_ERROR(hr)

            if (bDone) {
                SetEvent(hDoneEvent);
                return FALSE;
            }

        // WarnMessage("in timer CopyData %lu\n", numFramesAvailable);

            hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
        EXIT_ON_ERROR(hr)

        // hr = pCaptureClient->GetNextPacketSize(&packetLength);
        // EXIT_ON_ERROR(hr)
    }

    eventElapsed.End();

    VerboseMessage("VioAudio Serial elapsed %f, %f, %f ms\n", eventElapsed.Elapsed(), copyElapsed.Elapsed(), allElapsed.Elapsed());

    eventElapsed.Begin(eventElapsed.endTime);

Exit:

    return TRUE;
}
#include "timer.h"

static TimerMgr timerMgr;


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
    UINT32 packetLength = 0, transferLength = 0;
    BOOL bDone = FALSE;
    BYTE *pData;
    DWORD flags;
    HANDLE hAudioSamplesReadyEvent = NULL;
    HANDLE hTimerWakeUp = NULL;
    HANDLE hDoneEvent = NULL;
    REFERENCE_TIME hnsDefaultDevicePeriod = { 0 };
    LARGE_INTEGER dueTime = { 0 };
    LONG timerPeriod;
    BOOL bSuccess;
    HANDLE waitForArray[3] = { 0 };

    LatencyDebug allElapsed, eventElapsed, copyElapsed;

    //int core = 4;
    //auto mask = (static_cast<DWORD_PTR>(1) << core);//core number starts from 0
    //auto ret = SetThreadAffinityMask(GetCurrentThread(), mask);

    bSuccess = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    if (!bSuccess) {
        WarnMessage("Failed to change thread priority\n");
    }

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

    hr = pAudioClient->GetDevicePeriod(&hnsDefaultDevicePeriod, NULL);
    EXIT_ON_ERROR(hr)

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr)

    AdjustFormatTo16Bits(pwfx);

    hr = pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK/*0*/ | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        hnsRequestedDuration,
        0,
        pwfx,
        NULL);
    EXIT_ON_ERROR(hr)

        // Get the size of the allocated buffer.
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr)

    hAudioSamplesReadyEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    EXIT_ON_ERROR((hAudioSamplesReadyEvent != NULL) ? S_OK : S_FALSE)

    hr = pAudioClient->SetEventHandle(hAudioSamplesReadyEvent);
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

    hTimerWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
    EXIT_ON_ERROR(hTimerWakeUp != NULL ? S_OK : S_FALSE)

    dueTime.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
    timerPeriod = (LONG)hnsDefaultDevicePeriod / (10 * 1000); // (LONG)hnsDefaultDevicePeriod / 2 / (10 * 1000); // convert to milliseconds                  hnsActualDuration / REFTIMES_PER_MILLISEC / 25;

    bSuccess = SetWaitableTimer(hTimerWakeUp, &dueTime, timerPeriod, NULL, NULL, TRUE);
    EXIT_ON_ERROR(bSuccess ? S_OK : S_FALSE)

    hDoneEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    EXIT_ON_ERROR(hDoneEvent != NULL ? S_OK : S_FALSE)

    hr = pAudioClient->Start();  // Start recording.
    EXIT_ON_ERROR(hr)

	hr = pAudioSink->CopyData(
			NULL, 1920, TRUE, &bDone);
	EXIT_ON_ERROR(hr)

#if 0
    AudioSinkAux audioSinkAux;

    audioSinkAux.hDoneEvent = hDoneEvent;
    audioSinkAux.pAudioSink = pAudioSink;
    audioSinkAux.pCaptureClient = pCaptureClient;
    audioSinkAux.pwfx = pwfx;

    WarnMessage("timerMgr.Init() %s\n", timerMgr.Init() ? "true" : "false");

    WarnMessage("timerMgr.CreateAndAddTimer %s\n", timerMgr.CreateAndAddTimer(TimerQueueRoutine, timerPeriod, 0, &audioSinkAux) ? "true" : "false");

    WaitForSingleObject(hDoneEvent, INFINITE);
    
    goto Exit;
#endif

    waitForArray[0] = hAudioSamplesReadyEvent;
    waitForArray[1] = hTimerWakeUp;
    waitForArray[2] = hDoneEvent;

 
    // Each loop fills about half of the shared buffer.
    while (bDone == FALSE)
    {
        allElapsed.Begin();
        eventElapsed.Begin(allElapsed.beginTime);

        // DWORD waitStatus = WaitForSingleObject(hAudioSamplesReadyEvent, INFINITE);
        DWORD waitStatus = WaitForMultipleObjects(ARRAY_SIZE(waitForArray), waitForArray, FALSE, INFINITE);

        eventElapsed.End();
        copyElapsed.Begin(eventElapsed.endTime);

        switch (waitStatus)
        {
        case WAIT_OBJECT_0:
        case WAIT_OBJECT_0 + 1:

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

                transferLength = numFramesAvailable * pwfx->nBlockAlign;

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    //    pData = NULL;  // Tell CopyData to write silence.
                    bSilence = TRUE;
                }

                // Copy the available capture data to the audio sink.
                hr = pAudioSink->CopyData(
                    pData, transferLength, bSilence, &bDone);
                EXIT_ON_ERROR(hr)

                hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
                EXIT_ON_ERROR(hr)

                copyElapsed.End();
                // packetLength -= transferLength;
                /*hr = pCaptureClient->GetNextPacketSize(&packetLength);
                EXIT_ON_ERROR(hr)*/

                allElapsed.End(copyElapsed.endTime);

                VerboseMessage("VioAudio Serial elapsed %f, %f, %f ms\n", eventElapsed.Elapsed(), copyElapsed.Elapsed(), allElapsed.Elapsed());

                hr = pCaptureClient->GetNextPacketSize(&packetLength);
                if (FAILED(hr)) {
                    break;
                }
            }

            break;
        case WAIT_OBJECT_0 + 2:
            bDone = TRUE;
            break;
        default:
            break;
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

    if (hTimerWakeUp != NULL) {
        CancelWaitableTimer(hTimerWakeUp);
        CloseHandle(hTimerWakeUp);
    }

    if (hAudioSamplesReadyEvent != NULL) {
        CloseHandle(hAudioSamplesReadyEvent);
    }

    if (hDoneEvent != NULL) {
        CloseHandle(hDoneEvent);
    }

    return hr;
}

AudioAioSink::AudioAioSink(CDevice *device) :m_device(device) {
    m_ulBufferSize = 8192 * 8; // sizeof(m_pBuffer);
    m_ulBufferSizeMask = m_ulBufferSize - 1;

    m_i64Wpos = m_i64Rpos = 0;

    m_pBuffer = NULL;
}

AudioAioSink::~AudioAioSink() {
    if (m_hDoneEvent) {
        SetEvent(m_hDoneEvent);
    }

    if (m_hThread) {
        WaitForSingleObject(m_hThread, INFINITE);
        CloseHandle(m_hThread);
    }

    if (m_hDoneEvent != NULL) {
        CloseHandle(m_hDoneEvent);
    }

    if (m_hNotifyEvent != NULL) {
        CloseHandle(m_hNotifyEvent);
    }

    if (m_pBuffer) {
        delete[] m_pBuffer;
    }
}

BOOL AudioAioSink::Init()
{
    m_hDoneEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    m_hNotifyEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    m_pBuffer = new UINT8[m_ulBufferSize];

    return m_hDoneEvent != NULL && m_hNotifyEvent != NULL;
}

BOOL AudioAioSink::Open(CONST CHAR* Path)
{
    m_hThread = CreateThread(NULL, 0, WokerRoutine, this, 0, NULL);

    return m_hThread != NULL;
}

DWORD WINAPI AudioAioSink::WokerRoutine(
    LPVOID lpThreadParameter
)
{
    AudioAioSink *audioSink = (AudioAioSink*)lpThreadParameter;
    HANDLE        waitForArray[2] = { 0 };
    BOOL          bDone = FALSE;
    DWORD         waitStatus;

    if (!audioSink) {
        return 0;
    }

    waitForArray[0] = audioSink->m_hNotifyEvent;
    waitForArray[1] = audioSink->m_hDoneEvent;

    while (!bDone) {

        waitStatus = WaitForMultipleObjects(ARRAY_SIZE(waitForArray), waitForArray, FALSE, INFINITE);
        switch (waitStatus)
        {
        case WAIT_OBJECT_0:
            audioSink->FlushToFile();
            ResetEvent(waitForArray[0]);
            break;
        case WAIT_OBJECT_0 + 1:
            bDone = TRUE;
            break;
        default:
            break;
        }

    }

    return 0;
}

DWORD AudioAioSink::ProcessData(Processcallable callback, INT32 PeriodSize, PVOID pDest, DWORD dwSize)
{
    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer, i64Copied = 0;
    INT32 uiStart, uiChunk, uiProcessed;
    UINT8 *pBufferStart;

    pBufferStart = (UINT8*)pDest;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    i64ToTransfer = i64Wpos - i64Rpos;
    if (i64ToTransfer < 0) {
        i64ToTransfer += m_ulBufferSize;
    }

    if (dwSize >= 0) {
        i64ToTransfer = min(i64ToTransfer, dwSize);
    }

    if (!i64ToTransfer) {
        return 0;
    }

    while (i64ToTransfer) {
        uiStart = (UINT32)(i64Rpos & m_ulBufferSizeMask);
        uiChunk = (UINT32)min(i64ToTransfer, m_ulBufferSize - uiStart);

        uiChunk = min(uiChunk, PeriodSize);

        uiProcessed = callback(pBufferStart + i64Copied, m_pBuffer + uiStart, uiChunk);

        i64Copied     += uiProcessed;
        i64Rpos       += uiProcessed;
        i64ToTransfer -= uiProcessed;

        if (uiProcessed != uiChunk) {
            break;
        }
    }

    InterlockedExchange((LONG *)&m_i64Rpos, (LONG)(i64Rpos & m_ulBufferSizeMask));

    return (DWORD)i64Copied;
}

DWORD AudioAioSink::WriteToFileInner(PVOID pDest, PVOID pSrc, DWORD dwSize)
{
    BOOL bSuccess;
    DWORD dwWritten = 0;

    UNREFERENCED_PARAMETER(pDest);

    if (!m_hFile || INVALID_HANDLE_VALUE == m_hFile) {
        return 0;
    }

    bSuccess = WriteFile(m_hFile, pSrc, dwSize, &dwWritten, NULL);
    if (!bSuccess) {
        WarnMessage("Failed to write file for %ld\n", GetLastError());
    }

    return dwWritten;
}

DWORD AudioAioSink::FlushToFile()
{
    DWORD processed = 0;
    Processcallable callback = std::bind(&AudioAioSink::WriteToFileInner, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    LatencyDebug latency;
    static LatencyDebug lastLatency;

    latency.Begin();

    // return ProcessData(&AudioAioSink::WriteToFileInner, 1920, NULL, -1);
    processed = ProcessData(callback, 1920, NULL, -1);

    if (processed > 0) {
        latency.End();
        lastLatency.End(latency.endTime);

        WarnMessage("AudioAioSink elapsed %f, since last %f\n", latency.Elapsed(), lastLatency.Elapsed());

        lastLatency.Begin(latency.endTime);
    }

    return processed;
}

HRESULT AudioAioSink::CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone)
{
    INT64 i64Wpos, i64Rpos;
    INT64 i64ToTransfer, i64Copied = 0;
    INT32 uiStart, uiChunk;
    UINT8 *pBufferStart;

    pBufferStart = (UINT8*)pData;

    i64Wpos = InterlockedAdd((LONG *)&m_i64Wpos, 0);
    i64Rpos = InterlockedAdd((LONG *)&m_i64Rpos, 0);

    i64ToTransfer = i64Rpos - i64Wpos;
    if (i64ToTransfer <= 0) {
        i64ToTransfer += m_ulBufferSize;
    }

    i64ToTransfer = min(i64ToTransfer, numFramesAvailable);

    if (!i64ToTransfer) {
        WarnMessage("[AudioAioSink::CopyData OverRun]");

        // !!! TODO !!!
        // try flush ring buffer
    }

    while (i64ToTransfer) {
        uiStart = (INT32)(i64Wpos & m_ulBufferSizeMask);
        uiChunk = (INT32)(min(i64ToTransfer, m_ulBufferSize - uiStart));

        if (bSilence) {
            ZeroMemory(m_pBuffer + uiStart, uiChunk);
        }
        else {
            CopyMemory(m_pBuffer + uiStart, pBufferStart + i64Copied, uiChunk);
        }

        i64Copied     += uiChunk;
        i64Wpos       += uiChunk;
        i64ToTransfer -= uiChunk;
    }

    // InterlockedAdd((LONG *)&m_i64Wpos, (LONG)i64Copied);
    InterlockedExchange((LONG *)&m_i64Wpos, (LONG)(i64Wpos & m_ulBufferSizeMask));

    if (i64Copied) {
        SetEvent(m_hNotifyEvent);
    }

    //  WarnMessage("Success to write %u bytes, is silence %s\n",
    //     numFramesAvailable, bSilence ? "true" : "false");

    return S_OK;
}

BOOL AudioAioSink::WaitForWoker(DWORD dwMilliseconds)
{
    BOOL status = TRUE;

    if (m_hThread) {
        status = WaitForSingleObject(m_hThread, dwMilliseconds);
    }

    return status;
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

#pragma comment (lib, "Ws2_32.lib")

AudioAioTcpSink::AudioAioTcpSink(CDevice * device, int port) : AudioAioSink(device)
{
    this->port = port;
}

AudioAioTcpSink::~AudioAioTcpSink()
{
    if (wsaInited) {
        Shutdown();

        Close();

        WSACleanup();
    }

    if (hAccepterThread != NULL) {
        bDone = TRUE;
        WaitForSingleObject(hAccepterThread, INFINITE);
    }
}

BOOL AudioAioTcpSink::Init()
{
    BOOL             inited;
    int              iResult;
    WSADATA          wsaData;
    struct addrinfo *result = NULL;
    struct addrinfo  hints;

    inited = AudioAioSink::Init();
    if (!inited) {
        return FALSE;
    }

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        WarnMessage("WSAStartup failed with error: %d\n", iResult);
        return FALSE;
    }

    wsaInited = TRUE;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags    = AI_PASSIVE;

    // Resolve the server address and port
    iResult = ::getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        WarnMessage("getaddrinfo failed with error: %d\n", iResult);
        return FALSE;
    }

#if 0
    for (addrinfo *addr = result; addr != NULL; addr = addr->ai_next)
    {
        SOCKET listenSocket = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (listenSocket != INVALID_SOCKET)
        {
            bind(listenSocket, addr->ai_addr, (int)addr->ai_addrlen);
            listen(listenSocket, ...);
            // store listenSocket in a list for later use... 
        }
    }
#endif

    // Create a SOCKET for connecting to server
    listenSocket = ::socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listenSocket == INVALID_SOCKET) {
        WarnMessage("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        return FALSE;
    }

    int enable = 1;
    if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&enable, sizeof(int)) < 0) {
        WarnMessage("setsockopt(SO_REUSEADDR) failed\n");
    }

	//if (setsockopt(listenSocket, SOL_SOCKET, SO_REUSEPORT, (const void*)&enable, sizeof(int)) < 0) {
	//  WarnMessage("setsockopt(SO_REUSEPORT) failed\n");
	//}

    sockaddr_in addr = {};
    addr.sin_family = result->ai_family;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Setup the TCP listening socket
    // iResult = ::bind(listenSocket, result->ai_addr, (int)result->ai_addrlen);
    iResult = ::bind(listenSocket, (sockaddr*) &addr, sizeof(addr));
    if (iResult == SOCKET_ERROR) {
        char err_tips[256]; // #define errno WSAGetLastError()
        strerror_s(err_tips, sizeof(err_tips), WSAGetLastError());

        WarnMessage("bind failed with error: %ld(%s)\n", WSAGetLastError(), err_tips);
        freeaddrinfo(result);
        return FALSE;
    }

    freeaddrinfo(result);

    hAccepterThread = ::CreateThread(NULL, 0, AcceptRoutine, this, 0, NULL);
    inited = (hAccepterThread != NULL);

    return inited;
}

int AudioAioTcpSink::Shutdown()
{
    int iResult;

    for (const auto& clientSocket : clientSockes) {
        iResult = ::shutdown(clientSocket, SD_SEND);
        
        if (iResult == SOCKET_ERROR) {
            WarnMessage("shutdown socket(%d) failed with error: %ld\n", clientSocket, WSAGetLastError());
        }
    }

    return 0;
}

int AudioAioTcpSink::Close()
{
    if (listenSocket != INVALID_SOCKET) {
        ::closesocket(listenSocket);
        listenSocket = INVALID_SOCKET;
    }

    for (auto& clientSocket : clientSockes) {
        if (clientSocket != INVALID_SOCKET) {
            ::closesocket(clientSocket);
            clientSocket = INVALID_SOCKET;
        }
    }

    return 0;
}

int
AudioAioTcpSink::sendn(int fd, const void *vptr, size_t n)
{
    size_t		nleft;
    int		    nwritten;
    const char	*ptr;

    ptr = (const char*)vptr;
    nleft = n;
    while (nleft > 0) {
#ifndef WIN32
#define fd_errno errno

        if ((nwritten = write(fd, ptr, nleft)) <= 0)
#else
#define fd_errno WSAGetLastError()

        if ((nwritten = send(fd, ptr, nleft, 0)) <= 0)
#endif
        {
            if (nwritten < 0 && fd_errno == EINTR)
                nwritten = 0;		/* and call write() again */
            else
                return (-1);			/* error */
        }

        nleft -= nwritten;
        ptr   += nwritten;
    }

    return (n);
}

DWORD AudioAioTcpSink::SendToClients(PVOID pDest, PVOID pSrc, DWORD dwSize)
{
    int iSendResult;

    UNREFERENCED_PARAMETER(pDest);
    
    for (auto& clientSocket : clientSockes) {
        if (clientSocket == INVALID_SOCKET) {
            continue;
        }

        iSendResult = sendn(clientSocket, pSrc, dwSize);
        if (iSendResult == SOCKET_ERROR) {
            WarnMessage("send to socket(%d) failed with error: %d\n", clientSocket, WSAGetLastError());
            continue;
        }
    }

    return dwSize;
}

DWORD AudioAioTcpSink::SendBoth(PVOID pDest, PVOID pSrc, DWORD dwSize)
{
    DWORD written, sended;

    written = AudioAioSink::WriteToFileInner(pDest, pSrc, dwSize);
    if (!written){
    
    }

    sended = SendToClients(NULL, pSrc, dwSize);

    return max(written, sended);
}

DWORD AudioAioTcpSink::FlushToFile()
{
    DWORD processed = 0;
    Processcallable callback = std::bind(&AudioAioTcpSink::SendBoth, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);

    LatencyDebug latency;
    static LatencyDebug lastLatency;

    latency.Begin();

    // return ProcessData(&AudioAioSink::WriteToFileInner, 1920, NULL, -1);
    processed = ProcessData(callback, 1920, NULL, -1);

    if (processed > 0) {
        latency.End();
        lastLatency.End(latency.endTime);

        VerboseMessage("AudioAioTcpSink elapsed %f, since last %f\n", latency.Elapsed(), lastLatency.Elapsed());

        lastLatency.Begin(latency.endTime);
    }

    return processed;
}

DWORD __stdcall AudioAioTcpSink::AcceptRoutine(LPVOID lpThreadParameter)
{
    AudioAioTcpSink *tcpSink;
    int              status;
    
    tcpSink = static_cast<AudioAioTcpSink*>(lpThreadParameter);

    status = tcpSink->Listen();
    if (status < 0) {
        WarnMessage("Failed to listen socket for error(%d)\n", status);
        return -1;
    }

    while (!tcpSink->bDone){
        
        status = tcpSink->Accept();
        if (status < 0) {
            WarnMessage("Failed to accept socket for error(%d)\n", status);
            continue;
        }

    }

    return 0;
}

int AudioAioTcpSink::Listen()
{
    int iResult;

    iResult = ::listen(listenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        WarnMessage("listen failed with error: %d\n", WSAGetLastError());
        return -1;
    }

    return 0;
}

int AudioAioTcpSink::Accept()
{
    SOCKET clientSocket;

    // Accept a client socket
    clientSocket = ::accept(listenSocket, NULL, NULL);
    if (clientSocket == INVALID_SOCKET) {
        WarnMessage("accept failed with error: %d\n", WSAGetLastError());
        return -1;
    }

    clientSockes.push_back(clientSocket);

    return 0;
}
