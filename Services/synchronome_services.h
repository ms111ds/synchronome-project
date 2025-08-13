#include "../Libraries/common_library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <float.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <syslog.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include "../Libraries/v4l2_library.h"

#ifndef _SYNCHRONOME_SRV_H
#define _SYNCHRONOME_SRV_H

#define MY_SCHED_POLICY SCHED_FIFO
#define NUM_MSG_QUEUE_BUFS 5

#define RECORD_SERVICES_LOG 0
#define RECORD_IMG_CAPTURE_LOG 1

#define USE_COOL_BORDER 0
#define OUTPUT_YUYV_PPM 0

#if RECORD_SERVICES_LOG
#define SERVICE_LOG(str, arg1, arg2, arg3) \
    syslog( LOG_CRIT, (str), (arg1), (arg2), (arg3) )
#else // #if RECORD_SERVICES_LOG
// to remove "set but not used" warning for arg3
#define SERVICE_LOG(str, arg1, arg2, arg3) if(arg3 > 0.0){ }
#endif // #if RECORD_SERVICES_LOG #else

#if RECORD_IMG_CAPTURE_LOG
#define IMG_CAPTURE_LOG(str, arg1, arg2) syslog( LOG_CRIT, (str), (arg1), (arg2) )
#else // #if RECORD_IMG_CAPTURE_LOG
// to remove "set but not used" warning for arg2
#define IMG_CAPTURE_LOG(str, arg1, arg2) if(arg2 > 0.0){ }
#endif // #if RECORD_IMG_CAPTURE_LOG #else

#endif // #ifndef _SYNCHRONOME_SRV_H

bool run_synchronome( struct v4l2_state *state );
