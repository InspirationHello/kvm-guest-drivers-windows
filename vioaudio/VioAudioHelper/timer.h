#ifndef _TIMER_UTIL_H_
#define _TIMER_UTIL_H_

#include <Windows.h>

typedef BOOL(*TimerQueueRoutinePfn)(PVOID lpParam);

class TimerMgr;
struct Timer;

struct Timer {
    HANDLE hTimer = NULL;
    DWORD  dueTime = 0;
    DWORD  period = 0;
    PVOID  args = NULL;
    BOOL   Once = FALSE;

    TimerQueueRoutinePfn callback;
    TimerMgr *timerMgr = NULL;
    BOOL      selfAllocated = FALSE;
};

class TimerMgr {
public:
    TimerMgr() {

    }

    BOOL Init();

    BOOL AddTimer(Timer *timer);

    BOOL CreateAndAddTimer(TimerQueueRoutinePfn Callback, DWORD Period, BOOL Once, PVOID Args);

    BOOL DelTimer(Timer* timer);

private:
    HANDLE        m_hTimerQueue = NULL;

    static VOID CALLBACK TryQueueTimer(PVOID lpParam, BOOLEAN TimerOrWaitFired);

};

#endif