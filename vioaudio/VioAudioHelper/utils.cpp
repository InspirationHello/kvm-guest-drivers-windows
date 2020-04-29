#include "precomp.h"

void VioAudioLog(const char *cap, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s: ", cap);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void VioAudioLogW(const wchar_t *cap, const wchar_t *fmt, ...)
{
    va_list ap;

    fwprintf(stderr, L"%s: ", cap);

    va_start(ap, fmt);
    vfwprintf(stderr, fmt, ap);
    va_end(ap);
}


LONGLONG LatencyDebug::Frequency = 0;
