#ifndef VIOAUDIO_PRE_COMP_H_
#define VIOAUDIO_PRE_COMP_H_

// #define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <dbt.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>

#ifdef UNIVERSAL
#include <cfgmgr32.h>
#endif // UNIVERSAL

#include "public.h"
#include "utils.h"
#include "device.h"

#include <assert.h>

#include <vector>

#endif // !VIOAUDIO_PRE_COMP_H_
