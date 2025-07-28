#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>

#ifndef _V4L2_LIBRARY_H
#define _V4L2_LIBRARY_H

enum io_method 
{
        IO_METHOD_READ,
        IO_METHOD_MMAP,
        IO_METHOD_USERPTR,
};

struct buffer 
{
        void   *start;
        size_t  length;
};

struct v4l2_state {
    int fileDescriptor;
    char deviceName[64];
    enum io_method ioMethod;
    struct v4l2_format formatData;
    unsigned int numBuffers;
    struct buffer *bufferList;
};

#endif // #ifndef _V4L2_LIBRARY_H

void init_v4l2_state( struct v4l2_state *state );
bool open_device( char *deviceName, struct v4l2_state *state );
bool close_device( struct v4l2_state *state );
