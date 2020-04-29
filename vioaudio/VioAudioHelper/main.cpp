#define _CRT_SECURE_NO_WARNINGS

#include "precomp.h"

#include "audio_capture.h"

#include <fstream>  

typedef bool (*test_audio_pfn_t)(CDevice *device, const char *file_name);

bool test_write_file(CDevice *device, const char *file_name);
bool test_core_audio_loopback(CDevice *device, const char *file_name);
bool test_core_audio_loopback_virtio_serial(CDevice *device, const char *file_name);
bool test_core_audio_save_to_file(CDevice *device, const char *file_name);
bool test_core_audio_loopback_tcp(CDevice *device, const char *file_name);
bool test_audio_record(CDevice *device, const char *file_name);

enum TETS_OP_TYPE{
    DEFAULT_TEST_OP,
    TEST_WRITE_FILE,
    TEST_LOOPBACK,
    TEST_LOOPBACK_VIRTIO_SERIAL,
    TEST_LOOPBACK_TO_FILE,
    TEST_LOOPBACK_TO_TCP,
    TEST_RECORD,
};

struct op_cmd {
    TETS_OP_TYPE op_type;
    const wchar_t *opt;
    // callback(for ansi c) / functional(for cpp)
    test_audio_pfn_t callback;
    const wchar_t *desc;
};

static op_cmd op_cmds[] = {
    {TEST_WRITE_FILE, L"test_write", test_write_file, L"file_name"},
    {TEST_LOOPBACK, L"test_loopback", test_core_audio_loopback, L""}, // can use Virtual Audio Cable for test
    {TEST_LOOPBACK_VIRTIO_SERIAL, L"test_loopback_virtio_serial", test_core_audio_loopback_virtio_serial, L"file_name"}, // can use Virtual Audio Cable for test
    {TEST_LOOPBACK_TO_FILE, L"test_loopback_to_file", test_core_audio_save_to_file, L"file_name"}, // can use Virtual Audio Cable for test
    {TEST_LOOPBACK_TO_TCP, L"test_loopback_to_tcp", test_core_audio_loopback_tcp, L""}, // send pcm use tcp
    {TEST_RECORD, L"test_record", test_audio_record, L"file_name"}, // can read record pcm data from qemu
};

#define USE_WMAIN
#ifdef USE_WMAIN
int
_cdecl
wmain(
    __in              ULONG argc,
    __in_ecount(argc) PWCHAR argv[]
)
#else
int
_cdecl
main(
    __in              int argc,
    __in_ecount(argc) char argv[]
)
#endif // DEBUG

#if 0
{
    char file_name[256] = "test_file_buffer.wav";
    char buffer[256] = { 0 };
    const wchar_t *file_name_arg = NULL;
    TETS_OP_TYPE op = DEFAULT_TEST_OP;
    
    // snprintf(file_name, ARRAYSIZE(file_name), "%s", "test_file_buffer.wav");

    if (argc > 2 && !wcscmp(argv[1], L"-o")) {

        for (int i = 0; i < ARRAYSIZE(op_cmds); ++i) {
            op_cmd *cmd = op_cmds + i;

            if (!wcscmp(argv[2], cmd->opt)) {
                op = cmd->op_type;
                break;
            }
        }

        if (argc > 3) {
            file_name_arg = argv[3];
        }
    }
    else {
        if (argc > 1) {
            file_name_arg = argv[1];
        }
    }

    if (file_name_arg) {
        snprintf(file_name, ARRAYSIZE(file_name), "%ws", file_name_arg);
    }

    CDevice device;

    // device.Start();

    op_cmd *cmd = nullptr; // &op_cmds[op];
    // cmd->callback(&device, file_name);

    switch (op)
    {
    case TEST_WRITE_FILE:
    case TEST_LOOPBACK:
    case TEST_LOOPBACK_VIRTIO_SERIAL:
    case TEST_LOOPBACK_TO_FILE:
    case TEST_LOOPBACK_TO_TCP:
    case TEST_RECORD:

        cmd = &op_cmds[op - 1];

        WarnMessageW(L"%s to %s.\n", cmd->callback(&device, file_name) ? L"Success" : L"Failed", cmd->opt);

        break;
    default:
        for (int i = 0; i < ARRAY_SIZE(op_cmds); ++i) {
            cmd = &op_cmds[i];

            WarnMessageW(L"-o %s %s\n", cmd->opt, cmd->desc);
        }

        WarnMessage("%s to test write file.\n", test_write_file(&device, file_name) ? "Success" : "Failed");
        WarnMessage("%s to test loopback.\n", test_core_audio_loopback(&device, file_name) ? "Success" : "Failed");
        break;
    }
    
    return 0;
}
#else
{

	op_cmd* cmd = nullptr; // &op_cmds[op];
	// cmd->callback(&device, file_name);

	cmd = &op_cmds[TEST_LOOPBACK_VIRTIO_SERIAL - 1];
	cmd = &op_cmds[TEST_WRITE_FILE - 1];

	while (TRUE) {
		CDevice device(L"\\\\.\\Global\\com.thinputer.audio.0");

		// device.Start();

		WarnMessageW(L"%s to %s.\n", cmd->callback(&device, "C:\\only_for_dbg.pcm") ? L"Success" : L"Failed", cmd->opt);

		Sleep(1000);
	}

	return 0;
}
#endif

#include <time.h>

bool test_write_file(CDevice *device, const char *file_name)
{
    FILE * pcm_fp;
    long file_size, to_read;
    size_t read_rst = 0, readed = 0;
    bool success = true;

    pcm_fp = fopen(file_name, "rb");
    if (!pcm_fp) {
        WarnMessage("failed to open %s for read\n", file_name);
        return false;
    }

    // obtain file size:
    fseek(pcm_fp, 0, SEEK_END);
    file_size = ftell(pcm_fp);
    rewind(pcm_fp);

    to_read = file_size;

	#define SCALE (1)

    // char buffer[1920 * SCALE] = { 0 };
	const int buffer_size = 1920 * SCALE;
	char* buffer = new char[buffer_size];
    int retry = 3;

	HANDLE hTimerWakeUp = NULL;
	LARGE_INTEGER dueTime = { 0 };
	LONG timerPeriod;
	LONG hnsDefaultDevicePeriod = 10 * SCALE;
	BOOL bSuccess;

	hTimerWakeUp = CreateWaitableTimer(NULL, FALSE, NULL);
	if (hTimerWakeUp == NULL) {
		WarnMessage("Faile to CreateWaitableTimer\n");
		return false;
	}

	dueTime.QuadPart = -hnsDefaultDevicePeriod / 2; // negative means relative time
	timerPeriod = (LONG)hnsDefaultDevicePeriod / (10 * 1000);

	bSuccess = SetWaitableTimer(hTimerWakeUp, &dueTime, timerPeriod, NULL, NULL, TRUE);
	if (!bSuccess) {
		WarnMessage("Faile to SetWaitableTimer\n");
		return false;
	}

	clock_t begin, end;

    while (true) {
        read_rst = fread(buffer, 1, /*ARRAYSIZE(buffer)*/buffer_size, pcm_fp);
    
        if (!read_rst) {
            break;
        }
        else {
            if (device) {
				begin = clock();
                device->Write(buffer, read_rst);
				end = clock();

				fprintf(stdout, "write to virtio serial elapsed %f ms\n", double(end - begin) / CLOCKS_PER_SEC);
            }
        }

		WarnMessage("write %u bytes to device\n", read_rst);

		// Sleep(10 * SCALE);
		DWORD waitStatus = WaitForSingleObject(hTimerWakeUp, 10 * SCALE);
    }

    fclose(pcm_fp);

	delete []buffer;

    return success;
}

struct VioAudioSink : public AudioSink {
    VioAudioSink(CDevice *device):m_device(device){
        
    }

    ~VioAudioSink() {
    
    }

    BOOL Open(CONST CHAR* Path)
    {
        if (m_device) {
            // return m_device->Init();
        }

        return TRUE;
    }

    HRESULT CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone)
    {
        CONST CHAR *pToWrite = (CONST CHAR*)pData;
        static CONST CHAR zero_buffer[4096] = { 0 };

        if (bSilence) {
            pToWrite = zero_buffer;
        }

        DWORD dwToWrite = numFramesAvailable, dwWriteN = 0;

        while (dwToWrite) {
            dwWriteN = min(dwToWrite, ARRAYSIZE(zero_buffer));

            if (m_device) {
                m_device->Write((PVOID)pToWrite, dwWriteN);
            }

            if (!bSilence) {
                pToWrite += dwWriteN;
            }

            dwToWrite -= dwWriteN;
        }

        WarnMessage("Success to write %u bytes, is silence %s\n", 
            numFramesAvailable, bSilence ? "true" : "false");

        return S_OK;
    }

    CDevice *m_device;
};

bool test_core_audio_loopback(CDevice *device, const char *file_name)
{
    HRESULT hr;

    VioAudioSink audioSink(device);

    CoInitialize(NULL);

    hr = RecordAudioStream(&audioSink);

    if (hr != S_OK) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        WarnMessageW(L"Failed with: %s\n", errMsg);

        CoUninitialize();
        return false;
    }

    CoUninitialize();

    return true;
}

struct FileAudioSink : public AudioSink {
    std::ofstream m_stream;

    FileAudioSink(CDevice *device) :m_device(device) {

    }

    ~FileAudioSink() {
        if (m_stream.is_open()) {
            m_stream.close();
        }
    }

    BOOL Open(CONST CHAR* Path)
    {
        m_stream.open(Path, std::ios::out | std::ios::binary);
        if (!m_stream.is_open()){
            WarnMessage("Failed to open %s\n", Path);
            return FALSE;
        }

        return TRUE;
    }

    HRESULT CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone)
    {
        CONST CHAR *pToWrite = (CONST CHAR*)pData;
        static CONST CHAR zero_buffer[4096] = { 0 };

        if (bSilence) {
            pToWrite = zero_buffer;
        }

        DWORD dwToWrite = numFramesAvailable, dwWriteN = 0;

        while (dwToWrite) {
            dwWriteN = min(dwToWrite, ARRAYSIZE(zero_buffer));

            if (m_stream.is_open()) {
                m_stream.write(pToWrite, dwWriteN);
            }

            if (!bSilence) {
                pToWrite += dwWriteN;
            }

            dwToWrite -= dwWriteN;
        }

        WarnMessage("Success to write %u bytes, is silence %s\n",
            numFramesAvailable, bSilence ? "true" : "false");

        return S_OK;
    }

    CDevice *m_device;
};

bool test_core_audio_save_to_file(CDevice *device, const char *file_name)
{
    HRESULT hr;

    WarnMessage("call test_core_audio_save_to_file ......\n");

    FileAudioSink audioSink(NULL);

    audioSink.Open(file_name);

    CoInitialize(NULL);

    hr = RecordAudioStream(&audioSink);

    if (hr != S_OK) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        WarnMessage("Failed with: %s\n", errMsg);

        CoUninitialize();
        return false;
    }

    CoUninitialize();

    return true;
}

#if 1
struct VirtIOSerialAudioSink : public AudioAioSink {

    VirtIOSerialAudioSink(CDevice *device) :AudioAioSink(device), m_device(device) {

    }

    ~VirtIOSerialAudioSink() {
        if (isOK()) {
            CloseHandle(m_hFile);
        }
    }

    BOOL Open(CONST CHAR* Path)
    {
        AudioAioSink::Init();
        AudioAioSink::Open(Path);

        m_hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);

        if (m_hFile == INVALID_HANDLE_VALUE) {
            WarnMessage("VirtIOSerialAudioSink: error opening path %s\n", Path);
            return FALSE;
        }

        return TRUE;
    }

    BOOL isOK()
    {
        return m_hFile && m_hFile != INVALID_HANDLE_VALUE;
    }

    CDevice *m_device;
};
#else
struct VirtIOSerialAudioSink : public AudioSink {
    std::ofstream m_stream;

    HANDLE m_hFile;

    VirtIOSerialAudioSink(CDevice *device) : m_device(device) {

    }

    ~VirtIOSerialAudioSink() {
        if (isOK()) {
            CloseHandle(m_hFile);
        }
    }

    BOOL Open(CONST CHAR* Path)
    {
        m_hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);

        if (m_hFile == INVALID_HANDLE_VALUE) {
            WarnMessage("VirtIOSerialAudioSink: error opening path %s\n", Path);
            return FALSE;
        }

        return TRUE;
    }

    BOOL isOK() 
    {
        return m_hFile && m_hFile != INVALID_HANDLE_VALUE;
    }

    HRESULT CopyData(PVOID pData, DWORD numFramesAvailable, BOOL bSilence, BOOL *bDone)
    {
        CONST CHAR *pToWrite = (CONST CHAR*)pData;
        static CONST CHAR zero_buffer[4096] = { 0 };

        if (bSilence) {
            pToWrite = zero_buffer;
        }

        DWORD dwToWrite = numFramesAvailable, dwWriteN = 0;
        DWORD dwWritten;

        while (dwToWrite) {
            dwWriteN = min(dwToWrite, ARRAYSIZE(zero_buffer));

            if (isOK()) {
                if (!WriteFile(m_hFile, pToWrite, dwWriteN, &dwWritten, NULL)) {
                    WarnMessage("VirtIOSerialAudioSink: Failed to write %u bytes when %u, is silence %s\n",
                        numFramesAvailable, dwWriteN, bSilence ? "true" : "false");
                }
            }

            if (!bSilence) {
                pToWrite += dwWriteN;
            }

            dwToWrite -= dwWriteN;
        }

        /*WarnMessage("VirtIOSerialAudioSink: Success to write %u bytes, is silence %s\n",
            numFramesAvailable, bSilence ? "true" : "false");*/

        return S_OK;
    }

    CDevice *m_device;
};
#endif

bool test_core_audio_loopback_virtio_serial(CDevice *device, const char *file_name)
{
    HRESULT hr;

    WarnMessage("call test_core_audio_loopback_virtio_serial ......\n");

    VirtIOSerialAudioSink audioSink(NULL);

    /* \\\\.\\Global\\com.thinputer.audio.0 */

    const char *port_name = "\\\\.\\Global\\com.thinputer.audio.0";

    audioSink.Open(/*file_name*/port_name);

    CoInitialize(NULL);

    hr = RecordAudioStream(&audioSink);

    if (hr != S_OK) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        WarnMessage("Failed with: %s\n", errMsg);

        CoUninitialize();
        return false;
    }

    CoUninitialize();

    return true;
}

struct AudioTcpSink : public AudioAioTcpSink {
    /*for tcp channel */

    AudioTcpSink(CDevice *device) :AudioAioTcpSink(device, 8086) {

    }

    ~AudioTcpSink() {
        if (isOK()) {
            CloseHandle(m_hFile);
        }
    }

    BOOL Open(CONST CHAR* Path)
    {
        BOOL isSuccess;

        isSuccess = AudioAioTcpSink::Init();
        isSuccess = isSuccess && AudioAioTcpSink::Open(Path);

        if (Path && isSuccess) {
            m_hFile = CreateFileA(Path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, NULL);

            if (m_hFile == INVALID_HANDLE_VALUE) {
                WarnMessage("AudioTcpSink: error opening path %s\n", Path);
                return FALSE;
            }
        }

        return isSuccess;
    }

    BOOL isOK()
    {
        return m_hFile && m_hFile != INVALID_HANDLE_VALUE;
    }
};


bool test_core_audio_loopback_tcp(CDevice *device, const char *file_name)
{
    HRESULT hr;

    WarnMessage("call test_core_audio_loopback_tcp ......\n");

    AudioTcpSink audioSink(NULL);

    const char *port_name = "\\\\.\\Global\\com.thinputer.audio.0";

    audioSink.Open(/*NULL*/ port_name);

    CoInitialize(NULL);

    hr = RecordAudioStream(&audioSink);

    if (hr != S_OK) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        WarnMessage("Failed with: %s\n", errMsg);

        CoUninitialize();
        return false;
    }

    CoUninitialize();

    return true;
}

bool test_audio_record(CDevice *device, const char *file_name)
{
    char buffer[4096] = { 0 };
    DWORD dwReadN = 0, dwWritdN;
    FILE *pcm_fp;
    bool stop = false;

    pcm_fp = fopen(file_name, "wb");
    if (!pcm_fp) {
        WarnMessage("failed to open %s for write\n", file_name);
        return false;
    }

    if (!device) {
        WarnMessage("the device is null\n");
        return false;
    }

    while (!stop) {

        dwReadN = device->Read((PVOID)buffer, ARRAYSIZE(buffer));

        if (dwReadN) {
            dwWritdN = fwrite(buffer, sizeof(buffer[0]), dwReadN, pcm_fp);
            assert(dwWritdN == dwReadN);
        }

        Sleep(10); // sleep 10ms

        VerboseMessage("read %lu bytes\n", dwReadN);
    }

    fclose(pcm_fp);

    return true;
}