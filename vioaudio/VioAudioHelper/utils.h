#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

void VioAudioLog(const char *cap, const char *fmt, ...);

void VioAudioLogW(const wchar_t *cap, const wchar_t *fmt, ...);


#ifndef PrintMessage
#define PrintMessage(...) VioAudioLog ("VioAudio", __VA_ARGS__)
#else
#define PrintMessage(...)
#endif

#ifndef PrintMessageW
#define PrintMessageW(...) VioAudioLogW (L"VioAudio", __VA_ARGS__)
#else
#define PrintMessageW(...)
#endif

#endif
