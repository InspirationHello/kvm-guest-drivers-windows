#ifndef _MSVAD_EXPORT_H_
#define _MSVAD_EXPORT_H_

#include <msvad.h>

extern "C" NTSTATUS
VadDriverEntry
(
    IN  PDRIVER_OBJECT          DriverObject,
    IN  PUNICODE_STRING         RegistryPathName
);

#endif // !_MSVAD_EXPORT_H_
