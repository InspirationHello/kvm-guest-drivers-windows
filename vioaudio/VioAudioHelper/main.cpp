#define _CRT_SECURE_NO_WARNINGS

#include "precomp.h"

#include "audio_capture.h"

bool test_write_file(CDevice *device, const char *file_name);
bool test_core_audio_loopback(CDevice *device);

enum TETS_OP_TYPE{
    DEFAULT_TEST_OP,
    TETS_WRTE_FILE,
    TETS_LOOPBACK,
};

struct op_cmd {
    TETS_OP_TYPE op_type;
    const wchar_t *desc;
    // callback(for ansi c) / functional(for cpp)
};

static op_cmd op_cmds[] = {
    {TETS_WRTE_FILE, L"test_write"},
    {TETS_LOOPBACK, L"test_loopback"}, // can use Virtual Audio Cable for test
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
{
    char file_name[256] = "test_file_buffer.wav";
    char buffer[256] = { 0 };
    const wchar_t *file_name_arg = NULL;
    TETS_OP_TYPE op = DEFAULT_TEST_OP;
    
    // snprintf(file_name, ARRAYSIZE(file_name), "%s", "test_file_buffer.wav");

    if (argc > 2 && !wcscmp(argv[1], L"-o")) {

        for (int i = 0; i < ARRAYSIZE(op_cmds); ++i) {
            op_cmd *cmd = op_cmds + i;

            if (!wcscmp(argv[2], cmd->desc)) {
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

    switch (op)
    {
    case TETS_WRTE_FILE:
        PrintMessage("%s to test write file.\n", test_write_file(&device, file_name) ? "Success" : "Failed");
        break;
    case TETS_LOOPBACK:
        PrintMessage("%s to test loopback.\n", test_core_audio_loopback(&device) ? "Success" : "Failed");
        break;
    default:
        PrintMessage("%s to test write file.\n", test_write_file(&device, file_name) ? "Success" : "Failed");
        PrintMessage("%s to test loopback.\n", test_core_audio_loopback(&device) ? "Success" : "Failed");
        break;
    }
    
    return 0;
}

bool test_write_file(CDevice *device, const char *file_name)
{
    FILE * pcm_fp;
    long file_size, to_read;
    size_t read_rst = 0, readed = 0;
    bool success = true;

    pcm_fp = fopen(file_name, "rb");
    if (!pcm_fp) {
        PrintMessage("failed to open %s for read\n", file_name);
        return false;
    }

    // obtain file size:
    fseek(pcm_fp, 0, SEEK_END);
    file_size = ftell(pcm_fp);
    rewind(pcm_fp);

    to_read = file_size;

    char buffer[8192] = { 0 };
    int retry = 3;

    while (true) {
        read_rst = fread(buffer, 1, ARRAYSIZE(buffer), pcm_fp);
    
        if (!read_rst) {
            break;
        }
        else {
            if (device) {
                device->Write(buffer, read_rst);
            }
        }
    }

    fclose(pcm_fp);

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

        PrintMessage("Success to write %u bytes, is silence %s\n", 
            numFramesAvailable, bSilence ? "true" : "false");

        return S_OK;
    }

    CDevice *m_device;
};

bool test_core_audio_loopback(CDevice *device)
{
    HRESULT hr;

    VioAudioSink audioSink(device);

    CoInitialize(NULL);

    hr = RecordAudioStream(&audioSink);

    if (hr != S_OK) {
        _com_error err(hr);
        LPCTSTR errMsg = err.ErrorMessage();

        PrintMessage("Failed with: %s\n", errMsg);

        CoUninitialize();
        return false;
    }

    CoUninitialize();

    return true;
}
