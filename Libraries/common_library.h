#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif // #define _GNU_SOURCE

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#ifndef _COMMON_LIB_H
#define _COMMON_LIB_H


#endif // #ifndef _COMMON_LIB_H

void errno_print(const char *s);
double timespec_to_double_us( struct timespec *ts );
