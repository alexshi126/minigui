///////////////////////////////////////////////////////////////////////////////
//
//                          IMPORTANT NOTICE
//
// The following open source license statement does not apply to any
// entity in the Exception List published by FMSoft.
//
// For more information, please visit:
//
// https://www.fmsoft.cn/exception-list
//
//////////////////////////////////////////////////////////////////////////////
/*
 *   This file is part of MiniGUI, a mature cross-platform windowing
 *   and Graphics User Interface (GUI) support system for embedded systems
 *   and smart IoT devices.
 *
 *   Copyright (C) 2002~2018, Beijing FMSoft Technologies Co., Ltd.
 *   Copyright (C) 1998~2002, WEI Yongming
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Or,
 *
 *   As this program is a library, any link to this program must follow
 *   GNU General Public License version 3 (GPLv3). If you cannot accept
 *   GPLv3, you need to be licensed from FMSoft.
 *
 *   If you have got a commercial license of this program, please use it
 *   under the terms and conditions of the commercial license.
 *
 *   For more information about the commercial license, please refer to
 *   <http://www.minigui.com/blog/minigui-licensing-policy/>.
 */
/*
** timer.c: The Timer module for MiniGUI-Threads.
**
** Current maintainer: Wei Yongming.
**
** Create date: 1999/04/21
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "minigui.h"
#include "gdi.h"
#include "window.h"
#include "cliprect.h"
#include "gal.h"
#include "internals.h"
#include "misc.h"
#include "timer.h"

#ifdef _MGRM_PROCESSES
#include "ourhdr.h"
#include "client.h"
#include "sharedres.h"
#endif /* defined _MGRM_PROCESSES */

#if defined(_DEBUG) && defined(_MGRM_PROCESSES)
#define TIMER_ERR_SYS(text)         __mg_err_sys (text)
#else
#define TIMER_ERR_SYS(text)
#endif

DWORD __mg_timer_counter = 0;

/* update timer count for desktop thread */
void __mg_update_timer_count (void *data)
{
#if defined(_MGRM_PROCESSES)
    SHAREDRES_TIMER_COUNTER = __mg_os_get_time_ms()/10;
#else
    __mg_timer_counter = __mg_os_get_time_ms()/10;
#endif

    /* alert desktop */
    __mg_dsk_msg_queue->dwState |= QS_DESKTIMER;
}

#ifdef __NOUNIX__

static BOOL install_system_timer (void)
{
    return TRUE;
}

static BOOL unintall_system_timer (void)
{
    return TRUE;
}

#else   /* defined __NOUNIX__ */

#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

static struct sigaction old_alarm_handler;
static struct itimerval old_timer;

static BOOL install_system_timer (void)
{
    struct itimerval timerv;
    struct sigaction siga;

    sigaction (SIGALRM, NULL, &old_alarm_handler);

    siga = old_alarm_handler;
    siga.sa_handler = (void(*)(int))__mg_update_timer_count;
#ifndef _MGRM_STANDALONE
    siga.sa_flags = 0;
#else
    siga.sa_flags = SA_RESTART;
#endif
    sigaction (SIGALRM, &siga, NULL);

    timerv.it_interval.tv_sec = 0;
#if defined(__uClinux__) && defined(_MGRM_STANDALONE)
    timerv.it_interval.tv_usec = 100000;        /* 100 ms */
#else
    timerv.it_interval.tv_usec = 10000;         /* 10 ms */
#endif
    timerv.it_value = timerv.it_interval;

    if (setitimer (ITIMER_REAL, &timerv, &old_timer) == -1) {
        TIMER_ERR_SYS ("setitimer call failed!\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL unintall_system_timer (void)
{
    if (setitimer (ITIMER_REAL, &old_timer, 0) == -1) {
        TIMER_ERR_SYS ("setitimer call failed!\n");
        return FALSE;
    }

    if (sigaction (SIGALRM, &old_alarm_handler, NULL) == -1) {
        TIMER_ERR_SYS ("sigaction call failed!\n");
        return FALSE;
    }

    return TRUE;
}

#endif /* not defined __NOUNIX__ */

BOOL mg_InitTimer (BOOL use_sys_timer)
{
    __mg_timer_counter = 0;

    __mg_os_start_time_ms();

    if (use_sys_timer) {
        install_system_timer ();
    }

    return TRUE;
}

void mg_TerminateTimer (BOOL use_sys_timer)
{
    if (use_sys_timer)
        unintall_system_timer ();
}

/************************* Functions run in message thread *******************/
int __mg_check_expired_timers (MSGQUEUE* msg_queue, DWORD inter)
{
    int nr = 0;

    if (inter == 0)
        return 0;

    if (msg_queue == NULL) {
        msg_queue = getMsgQueueForThisThread ();
    }

    if (msg_queue) {
        int i;
        TIMER** timer_slots = msg_queue->timer_slots;
        DWORD ticks_current;
#ifdef _MGRM_PROCESSES
        ticks_current = SHAREDRES_TIMER_COUNTER;
#else
        ticks_current = __mg_timer_counter;
#endif

        for (i = 0; i < DEF_NR_TIMERS; i++) {
            if (timer_slots[i]) {
                if (ticks_current >= timer_slots[i]->ticks_expected) {
                    /* setting timer flag is simple, we do not need to lock
                       msgq, or else we may encounter dead lock here */
                    setMsgQueueTimerFlag (msg_queue, i);
                    timer_slots[i]->ticks_expected += timer_slots[i]->interv;
                    timer_slots[i]->ticks_fired = ticks_current;
                    nr++;
                }
#if 0   /* deprecated code */
                timer_slots[i]->count += inter;
                if (timer_slots[i]->count >= timer_slots[i]->interv) {
#ifdef _MGRM_PROCESSES
                    timer_slots[i]->ticks_current = SHAREDRES_TIMER_COUNTER;
#else
                    timer_slots[i]->ticks_current = __mg_timer_counter;
#endif
                    /* setting timer flag is simple, we do not need to lock msgq,
                       or else we may encounter dead lock here */
                    setMsgQueueTimerFlag (msg_queue, i);
                    timer_slots[i]->count -= timer_slots[i]->interv;
                    nr++;
                }
#endif /* deprecated code */
            }
        }
    }
    else {
        _WRN_PRINTF ("called for non message thread\n");
    }

    return nr;
}

BOOL GUIAPI SetTimerEx (HWND hWnd, LINT id, DWORD interv,
                TIMERPROC timer_proc)
{
    int i;
    int slot = -1;
    TIMER** timer_slots;
    PMSGQUEUE pMsgQueue;
    DWORD ticks_current;

    if (id == 0) {
        _WRN_PRINTF ("bad identifier (%ld).\n", id);
        return FALSE;
    }

    if (interv == 0) {
        interv = 1;
    }

    timer_slots = getTimerSlotsForThisThread (&pMsgQueue);
    if (MG_UNLIKELY (timer_slots == NULL)) {
        _WRN_PRINTF ("called for non message thread\n");
        goto badret;
    }

    if (MG_UNLIKELY (!isWindowInThisThread (hWnd))) {
        _WRN_PRINTF ("called for a window not in current thread\n");
        goto badret;
    }

    /* Is there an empty timer slot? */
    for (i = 0; i < DEF_NR_TIMERS; i++) {
        if (timer_slots[i] == NULL) {
            if (slot < 0)
                slot = i;
        }
        else if (timer_slots[i]->hWnd == hWnd && timer_slots[i]->id == id) {
            _WRN_PRINTF ("duplicated call to SetTimerEx (%p, %ld).\n", hWnd, id);
            goto badret;
        }
    }

    if (slot < 0 || slot == DEF_NR_TIMERS) {
        _WRN_PRINTF ("no more slot for new timer (total: %d)\n", DEF_NR_TIMERS);
        goto badret;
    }

    timer_slots[slot] = mg_slice_new (TIMER);

#ifdef _MGRM_PROCESSES
    ticks_current = SHAREDRES_TIMER_COUNTER;
#else
    ticks_current = __mg_timer_counter;
#endif

    timer_slots[slot]->hWnd = hWnd;
    timer_slots[slot]->id = id;
    timer_slots[slot]->interv = interv;
    timer_slots[slot]->ticks_expected = ticks_current + interv;
    timer_slots[slot]->ticks_fired = 0;
    timer_slots[slot]->proc = timer_proc;

    pMsgQueue->nr_timers++;

#ifdef _MGRM_PROCESSES
    if (!mgIsServer)
        __mg_set_select_timeout (USEC_10MS * interv);
#endif

    return TRUE;

badret:
    return FALSE;
}

#ifdef _MGRM_PROCESSES
static void reset_select_timeout (TIMER** timer_slots)
{
    int i;

    unsigned int interv = 0;
    for (i = 0; i < DEF_NR_TIMERS; i++) {
        if (timer_slots[i]) {
            if (interv == 0 || timer_slots[i]->interv < interv)
                interv = timer_slots[i]->interv;
        }
    }
    __mg_set_select_timeout (USEC_10MS * interv);
}
#endif

void __mg_remove_timer (MSGQUEUE* msg_queue, int slot)
{
    TIMER** timer_slots;

    if (slot < 0 || slot >= DEF_NR_TIMERS)
        return;

    timer_slots = msg_queue->timer_slots;
    if (MG_LIKELY (timer_slots[slot])) {
        /* The following code is already moved to message.c...
         * timer->msg_queue->expired_timer_mask &= ~(0x01 << slot);
         */
        mg_slice_delete (TIMER, timer_slots[slot]);
        timer_slots [slot] = NULL;
        msg_queue->nr_timers--;

#ifdef _MGRM_PROCESSES
        if (!mgIsServer)
            reset_select_timeout (timer_slots);
#endif
    }
}

void __mg_remove_timers_by_msg_queue (MSGQUEUE* msg_queue)
{
    int i;
    TIMER** timer_slots = msg_queue->timer_slots;

    for (i = 0; i < DEF_NR_TIMERS; i++) {
        if (timer_slots [i]) {
            mg_slice_delete (TIMER, timer_slots[i]);
            timer_slots [i] = NULL;
        }
    }

    msg_queue->nr_timers--;
}

/* If id == 0, clear all timers of the window */
int GUIAPI KillTimer (HWND hWnd, LINT id)
{
    int i;
    int killed = 0;
    MSGQUEUE* msg_queue;
   
    msg_queue = getMsgQueueForThisThread ();
    if (msg_queue) {

        TIMER** timer_slots =  msg_queue->timer_slots;
        for (i = 0; i < DEF_NR_TIMERS; i++) {
            if ((timer_slots [i] && timer_slots [i]->hWnd == hWnd) &&
                    (id == 0 || timer_slots [i]->id == id)) {
                removeMsgQueueTimerFlag (msg_queue, i);
                mg_slice_delete (TIMER, timer_slots[i]);
                timer_slots [i] = NULL;
                msg_queue->nr_timers--;
                killed ++;

                if (id)
                    break;
            }
        }

#ifdef _MGRM_PROCESSES
        if (!mgIsServer && killed)
            reset_select_timeout (msg_queue->timer_slots);
#endif
    }
    else {
        _WRN_PRINTF ("called for non message thread\n");
    }


    return killed;
}

BOOL GUIAPI ResetTimerEx (HWND hWnd, LINT id, DWORD interv,
                TIMERPROC timer_proc)
{
    int i;
    MSGQUEUE* msg_queue;
   
    if (id == 0)
        return FALSE;

    msg_queue = getMsgQueueForThisThread ();
    if (MG_LIKELY (msg_queue)) {
        TIMER** timer_slots = msg_queue->timer_slots;

        for (i = 0; i < DEF_NR_TIMERS; i++) {
            if (timer_slots[i] &&
                    timer_slots[i]->hWnd == hWnd && timer_slots[i]->id == id) {
                DWORD ticks_current;

#ifdef _MGRM_PROCESSES
                ticks_current = SHAREDRES_TIMER_COUNTER;
#else
                ticks_current = __mg_timer_counter;
#endif
                /* Should clear old timer flags */
                removeMsgQueueTimerFlag (msg_queue, i);
                timer_slots[i]->interv = interv;
                timer_slots[i]->ticks_expected = ticks_current + interv;
                timer_slots[i]->ticks_fired = 0;
                if (timer_proc != (TIMERPROC)INV_PTR)
                    timer_slots[i]->proc = timer_proc;

                return TRUE;
            }
        }
    }
    else {
        _WRN_PRINTF ("called for non message thread\n");
    }

    return FALSE;
}

BOOL GUIAPI IsTimerInstalled (HWND hWnd, LINT id)
{
    int i;
    TIMER** timer_slots;

    if (id == 0)
        return FALSE;

    timer_slots = getTimerSlotsForThisThread (NULL);
    if (timer_slots) {
        for (i = 0; i < DEF_NR_TIMERS; i++) {
            if (timer_slots[i] &&
                    timer_slots[i]->hWnd == hWnd && timer_slots[i]->id == id) {
                return TRUE;
            }
        }
    }
    else {
        _WRN_PRINTF ("called for non message thread\n");
    }

    return FALSE;
}

BOOL GUIAPI HaveFreeTimer (void)
{
    int i;
    TIMER** timer_slots;

    timer_slots = getTimerSlotsForThisThread (NULL);
    if (timer_slots) {
        for (i = 0; i < DEF_NR_TIMERS; i++) {
            if (timer_slots[i] == NULL)
                return TRUE;
        }
    }
    else {
        _WRN_PRINTF ("called for non message thread\n");
    }

    return FALSE;
}

DWORD GUIAPI GetTickCount (void)
{
#ifdef _MGRM_PROCESSES
    return SHAREDRES_TIMER_COUNTER;
#elif defined(_MGRM_STANDALONE)
    __mg_timer_counter = __mg_os_get_time_ms()/10;
    return __mg_timer_counter;
#else
    return __mg_timer_counter;
#endif
}

#if 0   /* deprecated code */

/* Since 5.0.0, we no longer use the timer thread for MiniGUI-Threads runmode */
   in message queue */

/* timer entry for thread version */
#ifdef _MGRM_THREADS

#ifdef __AOS__

#include "os_api.h"

static OS_TIMER_ID __mg_os_timer = 0;

#else /* __AOS__ */
static void* TimerEntry (void* data)
{
    sem_post ((sem_t*)data);

    while (__mg_quiting_stage > _MG_QUITING_STAGE_TIMER) {
        __mg_os_time_delay (10);
        __mg_update_timer_count (NULL);
    }

    /* printf("quit from TimerEntry()\n"); */
    return NULL;
}
#endif /* !__AOS__ */

int __mg_timer_init (void)
{
    if (!mg_InitTimer ()) {
        fprintf (stderr, "KERNEL>timer: Init Timer failure.\n");
        return -1;
    }

#ifdef __AOS__
    __mg_os_timer = tp_os_timer_create ("mgtimer", __mg_update_timer_count,
                                         NULL, AOS_TIMER_TICKT,
                                         OS_AUTO_ACTIVATE | OS_AUTO_LOAD);
#else /* __AOS__ */
    {
        sem_t wait;
        sem_init (&wait, 0, 0);
        pthread_create (&__mg_timer, NULL, TimerEntry, &wait);
        sem_wait (&wait);
        sem_destroy (&wait);
    }
#endif /* !__AOS__ */

    return 0;
}

void mg_TerminateTimer (void)
{
#ifdef __AOS__
    tp_os_timer_delete (__mg_os_timer);
#endif

#ifdef __WINBOND_SWLINUX__
    pthread_detach (__mg_timer); /* XXX: Can we pthread_join()? */
#else
    pthread_join (__mg_timer, NULL);
#endif /* __WINBOND_SWLINUX__ */

#ifdef _MGHAVE_VIRTUAL_WINDOW
    pthread_mutex_destroy (&timerLock);
#endif

#if 0   /* deprecated code */
    /* Since 5.0.0, we allocate timer slots per thread, and manage the time slots
       in message queue */
    for (i = 0; i < DEF_NR_TIMERS; i++) {
        if (timerstr[i] != NULL)
            free ( timerstr[i] );
        timerstr[i] = NULL;
    }

#ifdef _MGHAVE_VIRTUAL_WINDOW
    pthread_mutex_destroy (&timerLock);
#endif
#endif  /* deprecated code */
}

#else /* defined _MGRM_THREADS */
#endif /* defined _MGRM_THREADS */

/* Since 5.0.0, we use timer slots per thread, and manage the time slots
   in message queue */
static TIMER *timerstr[DEF_NR_TIMERS];

#ifdef _MGHAVE_VIRTUAL_WINDOW
/* lock for protecting timerstr */
static pthread_mutex_t timerLock;
#define TIMER_LOCK()   pthread_mutex_lock(&timerLock)
#define TIMER_UNLOCK() pthread_mutex_unlock(&timerLock)
#else
#define TIMER_LOCK()
#define TIMER_UNLOCK()
#endif

int __mg_get_timer_slot (HWND hWnd, int id)
{
    int i;
    int slot = -1;

    TIMER_LOCK ();
    for (i = 0; i < DEF_NR_TIMERS; i++) {
        if (timerstr[i] != NULL) {
            if (timerstr[i]->hWnd == hWnd && timerstr[i]->id == id) {
                slot = i;
                break;
            }
        }
    }

    TIMER_UNLOCK ();
    return slot;
}

void __mg_move_timer_last (TIMER* timer, int slot)
{
    if (slot < 0 || slot >= DEF_NR_TIMERS)
        return;

    TIMER_LOCK ();

    if (timer && timer->msg_queue) {
        /* The following code is already called in message.c...
         * timer->ticks_current = 0;
         * timer->msg_queue->expired_timer_mask &= ~(0x01 << slot);
         */

        if (slot != (DEF_NR_TIMERS - 1)) {
            TIMER* t;

            if (timer->msg_queue->expired_timer_mask & (0x01 << (DEF_NR_TIMERS -1))) {
                timer->msg_queue->expired_timer_mask |= (0x01 << slot);
                timer->msg_queue->expired_timer_mask &= ~(0x01 << (DEF_NR_TIMERS -1));
            }

            t = timerstr [DEF_NR_TIMERS - 1];
            timerstr [DEF_NR_TIMERS - 1] = timerstr [slot];
            timerstr [slot] = t;
        }
    }

    TIMER_UNLOCK ();
    return;
}

TIMER* __mg_get_timer (int slot)
{
    TIMER** timer_slots;

    if (slot < 0 || slot >= DEF_NR_TIMERS)
        return NULL;

    timer_slots = getTimerSlotsForThisThread (NULL);
    if (timer_slots) {
        return timer_slots[slot];
    }

    _WRN_PRINTF ("called for non message thread\n");
    return NULL;
}

#endif  /* deprecated code */

