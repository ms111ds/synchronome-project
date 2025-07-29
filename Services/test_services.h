#include "../Libraries/common_library.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <mqueue.h>
#include <pthread.h>
#include <sched.h>
#include <syslog.h>
#include <sched.h>
#include "../Libraries/v4l2_library.h"

#ifndef _TEST_SRV_H
#define _TEST_SRV_H
#define MY_SCHED_POLICY SCHED_FIFO
#endif // #ifndef _TEST_SRV_H

bool run_test_services( struct v4l2_state *state );
