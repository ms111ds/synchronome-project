#include "common_library.h"


#ifndef _COMMON_LIB_C
#define _COMMON_LIB_C

void errno_print(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
}

#endif // #ifndef _COMMON_LIB_C
