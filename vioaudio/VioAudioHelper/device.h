#ifndef DEVICE_H
#define DEVICE_H

#include <wtypes.h>

class CDevice {
public:
    CDevice();
    ~CDevice();
    DWORD Init();
    VOID Fini();
    BOOL Start();
    VOID Stop();

    DWORD Write(PVOID Buffer, DWORD Size);
    DWORD Read(PVOID Buffer, DWORD Size);
    DWORD IoCtrlFrom(DWORD CtrlCode, PVOID OutBuffer, DWORD OutSize);
    DWORD IoCtrlTo(DWORD CtrlCode, PVOID InBuffer, DWORD InSize);

protected:
    PTCHAR  GetDevicePath(IN LPGUID InterfaceGuid);

private:
    HANDLE m_hDevice;
};

#endif
