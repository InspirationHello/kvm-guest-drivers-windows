#ifndef UTILS_H
#define UTILS_H

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

void VioAudioLog(const char *cap, const char *fmt, ...);

void VioAudioLogW(const wchar_t *cap, const wchar_t *fmt, ...);

#ifdef _DEBUG
// #define DEBUG_LATENCY
// #define ENABLE_VERBOSE_LOG
#endif // DEBUG

#define WarnMessage(fmt, ...)   VioAudioLog ("VioAudio", "%s(%d): " fmt, __FILE__, __LINE__, ## __VA_ARGS__)
#define WarnMessageW(...)  VioAudioLogW (L"VioAudio", ## __VA_ARGS__)

#ifdef ENABLE_VERBOSE_LOG
#define VerboseMessage(...) VioAudioLog ("VioAudio", ## __VA_ARGS__)
#else
#define VerboseMessage(...)
#endif

#ifdef ENABLE_VERBOSE_LOG
#define VerboseMessageW(...) VioAudioLogW (L"VioAudio", ## __VA_ARGS__)
#else
#define VerboseMessageW(...)
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))
#endif

// #define DEBUG_LATENCY

struct LatencyDebug {

    static  LONGLONG Frequency;
    LONGLONG beginTime, endTime;

#define GET_ELAPSED(begin, end) ((double)(((double)(end - begin) * 1000.0) / Frequency))

    LatencyDebug() :beginTime(0), endTime(0) {
        if (!Frequency) {
            LARGE_INTEGER freq;

            if (QueryPerformanceFrequency(&freq)) {
                Frequency = freq.QuadPart;
            }
        }
    }

    LatencyDebug(LONGLONG Begin) {
        beginTime = Begin;

        LatencyDebug();
    }

    inline void Begin()
    {
        beginTime = PerformanceCounter();
    }

    inline void Begin(LONGLONG Time)
    {
        beginTime = Time;
    }

    inline void End()
    {
        endTime = PerformanceCounter();
    }

    inline void End(LONGLONG Time)
    {
        endTime = Time;
    }

    inline double Elapsed()
    {
#ifdef  DEBUG_LATENCY
        return GET_ELAPSED(beginTime, endTime);
#else
        return 0;
#endif //  DEBUG_LATENCY
    }

    inline long long PerformanceCounter() noexcept
    {
#ifdef  DEBUG_LATENCY
        LARGE_INTEGER li;
        ::QueryPerformanceCounter(&li);
        return li.QuadPart;
#else
        return 0LL;
#endif //  DEBUG_LATENCY
    }
};


#endif
