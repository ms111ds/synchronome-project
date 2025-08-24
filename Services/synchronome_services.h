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


/*******************************************************************************
 * LOGGING
 ******************************************************************************/
#define RECORD_SERVICES_LOG 0
#define RECORD_IMG_CAPTURE_LOG 0
#define DUMP_DIFFS 0
#define RECORD_QUEUE_SIZE 0


/*******************************************************************************
 * FEATURES
 ******************************************************************************/
#define OPERATE_AT_10HZ 0  // Default: Synchronizes to clock running at 1Hz.
#define USE_COOL_BORDER 1  // Default: No cool border applied to saved images.
#define OUTPUT_YUYV_PPM 0  // Default: Images saved are formatted as RGB.

/*******************************************************************************
 * FUNCTIONAL
 ******************************************************************************/
#define NUM_PICS 181
#define MY_SCHED_POLICY SCHED_FIFO
#define NUM_MSG_QUEUE_BUFS 32 // Must be this large as sometimes writing to
                              // flash stalls. This causes the queue to get
                              // filled up quite a bit.

#endif // #ifndef _SYNCHRONOME_SRV_H

bool run_synchronome( struct v4l2_state *state );
