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
#endif // #ifndef _SYNCHRONOME_SRV_H

bool run_synchronome( struct v4l2_state *state );
