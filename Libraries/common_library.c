#include "common_library.h"


#ifndef _COMMON_LIB_C
#define _COMMON_LIB_C

void errno_print(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
}

/******************************************************************************
 *
 * timespec_to_double_sec
 *
 * Description: Conversts a struct timespec value to a double precision
 *              floating point representing seconds.
 *
 * Arguments:   ts (IN): struct timespec value to be converted to floating
 *                       point (double precision).
 *
 * Return:      Floating point number representing seconds.
 *
 *****************************************************************************/
double timespec_to_double_sec( struct timespec *ts )
{
    return ( (double)ts->tv_sec ) +
           ( (double)ts->tv_nsec / 1000000000.0 );
}

/******************************************************************************
 *
 * timespec_to_double_us
 *
 * Description: Conversts a struct timespec value to a double precision
 *              floating point representing microseconds.
 *
 * Arguments:   ts (IN): struct timespec value to be converted to floating
 *                       point (double precision).
 *
 * Return:      Floating point number representing microseconds.
 *
 *****************************************************************************/
double timespec_to_double_us( struct timespec *ts )
{
    return ( (double)ts->tv_sec * 1000000.0 ) +
           ( (double)ts->tv_nsec / 1000.0 );
}


#endif // #ifndef _COMMON_LIB_C
