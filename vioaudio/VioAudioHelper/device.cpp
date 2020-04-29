#include "precomp.h"

CDevice::CDevice(const wchar_t* Path) :
m_hDevice(INVALID_HANDLE_VALUE)
{
    Init(Path);
}

CDevice::~CDevice()
{
    Fini();
}

DWORD CDevice::Init(const wchar_t* Path)
{
    PWCHAR DevicePath = (Path && wcslen(Path) > 0) ? _wcsdup(Path) : GetDevicePath((LPGUID)&GUID_DEVINTERFACE_VioAudio);
    if (DevicePath == NULL) {
        WarnMessage("File not found.\n");
        return ERROR_FILE_NOT_FOUND;
    }

    WarnMessageW(L"the DevicePath: %s\n", DevicePath);
    
    m_hDevice = CreateFile(DevicePath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0);
    
    free(DevicePath);

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        WarnMessage("Failed to create file.\n");
        return GetLastError();
    }

    return 0;
}

VOID CDevice::Fini()
{
    Stop();

    if (m_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hDevice);
		m_hDevice = INVALID_HANDLE_VALUE;
    }
}


BOOL CDevice::Start()
{
    ULONG retedLen = 0;

    char test_write[]= "123456123456123456123456123456123456";
    
    if (IoCtrlTo(IOCTL_VIOAUDIO_SEND_DATA, test_write, sizeof(test_write)) > 0) {
        WarnMessage("Test write succeeded.\n");
    }

    if (!Write(test_write, sizeof(test_write))) {
        char log_info[4096] = { 0 };
        snprintf(log_info, ARRAYSIZE(log_info), "Test WriteFile failed 0X%08x.\n", GetLastError());
        WarnMessage("%s", log_info);
    }

    char buffer[256] = { 0 };

    if (IoCtrlFrom(IOCTL_VIOAUDIO_GET_DATA, buffer, sizeof(buffer)) > 0) {
        char log_info[4096] = { 0 };
        snprintf(log_info, ARRAYSIZE(log_info), "the content: %s\n", buffer);

        WarnMessage("%s", log_info);
    }
    else {
        WarnMessage("Failed to DeviceIoControl IOCTL_VIOAUDIO_GET_DATA\n");
    }
   
    return TRUE;
}

VOID CDevice::Stop()
{
    
}

DWORD CDevice::Write(PVOID Buffer, DWORD Size)
{
    ULONG retedLen = 0;

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        WarnMessage("the device handle is invalid.\n");
        return 0;
    }

    if (!WriteFile(m_hDevice, Buffer, Size, &retedLen, NULL)) {
        WarnMessage("WriteFile failed with 0X%08x.\n", GetLastError());
        return 0;
    }

    return retedLen;
}

DWORD CDevice::Read(PVOID Buffer, DWORD Size)
{
    ULONG retedLen = 0;

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        WarnMessage("the device handle is invalid.\n");
        return 0;
    }

    if (!ReadFile(m_hDevice, Buffer, Size, &retedLen, NULL)) {
        WarnMessage("ReadFile failed with 0X%08x.\n", GetLastError());
        return 0;
    }

    return retedLen;
}

DWORD CDevice::IoCtrlFrom(DWORD CtrlCode, PVOID OutBuffer, DWORD OutSize)
{
    ULONG retedLen = 0;

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        WarnMessage("the device handle is invalid.\n");
        return 0;
    }

    if (DeviceIoControl(m_hDevice, CtrlCode, NULL, 0, OutBuffer, OutSize, &retedLen, NULL)) {
        WarnMessage("IoCtrlFrom failed with 0X%08x.\n", GetLastError());
        return 0;
    }

    return retedLen;
}

DWORD CDevice::IoCtrlTo(DWORD CtrlCode, PVOID InBuffer, DWORD InSize)
{
    ULONG retedLen = 0;

    if (m_hDevice == INVALID_HANDLE_VALUE) {
        WarnMessage("the device handle is invalid.\n");
        return 0;
    }

    if (DeviceIoControl(m_hDevice, CtrlCode, NULL, 0, InBuffer, InSize, &retedLen, NULL)) {
        WarnMessage("IoCtrlTo failed with 0X%08x.\n", GetLastError());
        return 0;
    }

    return retedLen;
}

#ifndef UNIVERSAL

PTCHAR CDevice::GetDevicePath( IN  LPGUID InterfaceGuid )
{
    HDEVINFO HardwareDeviceInfo;
    SP_DEVICE_INTERFACE_DATA DeviceInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA DeviceInterfaceDetailData = NULL;
    ULONG Length, RequiredLength = 0;
    BOOL bResult;
    PTCHAR DevicePath;

    HardwareDeviceInfo = SetupDiGetClassDevs(
                             InterfaceGuid,
                             NULL,
                             NULL,
                             (DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
                             );

    if (HardwareDeviceInfo == INVALID_HANDLE_VALUE) {
        WarnMessage("Cannot get class devices\n");
        return NULL;
    }

    DeviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    bResult = SetupDiEnumDeviceInterfaces(
                             HardwareDeviceInfo,
                             0,
                             InterfaceGuid,
                             0,
                             &DeviceInterfaceData
                             );

    if (bResult == FALSE) {
        WarnMessage("Cannot get enumerate device interfaces\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    SetupDiGetDeviceInterfaceDetail(
                             HardwareDeviceInfo,
                             &DeviceInterfaceData,
                             NULL,
                             0,
                             &RequiredLength,
                             NULL
                             );

    DeviceInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) LocalAlloc(LMEM_FIXED, RequiredLength);

    if (DeviceInterfaceDetailData == NULL) {
        WarnMessage("Cannot allocate memory\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        return NULL;
    }

    DeviceInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

    Length = RequiredLength;

    bResult = SetupDiGetDeviceInterfaceDetail(
                             HardwareDeviceInfo,
                             &DeviceInterfaceData,
                             DeviceInterfaceDetailData,
                             Length,
                             &RequiredLength,
                             NULL
                             );

    if (bResult == FALSE) {
        WarnMessage("Cannot get device interface details\n");
        SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
        LocalFree(DeviceInterfaceDetailData);
        return NULL;
    }

    DevicePath = _tcsdup(DeviceInterfaceDetailData->DevicePath);

    SetupDiDestroyDeviceInfoList(HardwareDeviceInfo);
    LocalFree(DeviceInterfaceDetailData);

    return DevicePath;
}

#else // UNIVERSAL

PTCHAR CDevice::GetDevicePath(IN LPGUID InterfaceGuid)
{
    PWSTR DeviceInterfaceList = NULL;
    ULONG DeviceInterfaceListLength = 0;
    PTCHAR DevicePath = NULL;
    CONFIGRET cr;

    do
    {
        cr = CM_Get_Device_Interface_List_Size(&DeviceInterfaceListLength,
            InterfaceGuid, NULL, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        if (cr != CR_SUCCESS)
        {
            break;
        }

        if (DeviceInterfaceList != NULL)
        {
            HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
        }

        DeviceInterfaceList = (PWSTR)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, DeviceInterfaceListLength * sizeof(WCHAR));

        if (DeviceInterfaceList == NULL)
        {
            cr = CR_OUT_OF_MEMORY;
            break;
        }

        cr = CM_Get_Device_Interface_List(InterfaceGuid, NULL,
            DeviceInterfaceList, DeviceInterfaceListLength,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

    } while (cr == CR_BUFFER_SMALL);

    if (cr == CR_SUCCESS)
    {
        DevicePath = _tcsdup(DeviceInterfaceList);
    }

    if (DeviceInterfaceList != NULL)
    {
        HeapFree(GetProcessHeap(), 0, DeviceInterfaceList);
    }

    return DevicePath;
}

#endif // UNIVERSAL
