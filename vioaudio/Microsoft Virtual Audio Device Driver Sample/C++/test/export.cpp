#include "export.h"

#include "adapter.h"

extern "C" NTSTATUS
VadDriverEntry
(
    IN  PDRIVER_OBJECT          DriverObject,
    IN  PUNICODE_STRING         RegistryPathName
)
{
    return AudioDeviceDriverEntry(DriverObject, RegistryPathName);
}