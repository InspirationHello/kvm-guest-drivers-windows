#include "timer.h"

VOID TimerMgr::TryQueueTimer(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
    Timer *timer = (Timer*)lpParam;
    BOOL isContinue = FALSE;

    UNREFERENCED_PARAMETER(TimerOrWaitFired);

    if (timer->callback) {
        isContinue = !timer->Once && timer->callback(timer->args);
    }

    if (isContinue) {
        // timer->timerMgr->AddTimer(timer);
    }
    else {
        timer->timerMgr->DelTimer(timer);

        if (timer->selfAllocated) {
            delete timer;
        }
    }
}

BOOL TimerMgr::Init()
{
    m_hTimerQueue = CreateTimerQueue();
    if (!m_hTimerQueue) {
        return FALSE;
    }

    return TRUE;
}

BOOL TimerMgr::AddTimer(Timer *timer)
{
    timer->timerMgr = this;

    return CreateTimerQueueTimer(&timer->hTimer, m_hTimerQueue,
        (WAITORTIMERCALLBACK)TryQueueTimer, timer, timer->dueTime, timer->period, WT_EXECUTEDEFAULT);
}

BOOL TimerMgr::CreateAndAddTimer(TimerQueueRoutinePfn Callback, DWORD Period, BOOL Once, PVOID Args)
{
    Timer *timer;

    timer = new Timer;
    if (!timer) {
        return FALSE;
    }

    timer->selfAllocated = TRUE;
    timer->dueTime = Period;
    timer->period = Period;
    timer->args = Args;
    timer->Once = Once;
    timer->callback = Callback;

    return AddTimer(timer);
}

BOOL TimerMgr::DelTimer(Timer* timer)
{
    return DeleteTimerQueueTimer(m_hTimerQueue, timer->hTimer, NULL);
}
