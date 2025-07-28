#include "v4l2_library.h"

#ifndef _V4L2_LIBRARY_C
#define _V4L2_LIBRARY_C

/******************************************************************************
 *
 * open_device
 *
 *
 * Description: opens the video device for use by this program.
 *
 * Arguments:   fileDescriptor - pointer to the file descriptor of the opened
 *                               video device
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool open_device( int *fileDescriptor )
{
    struct stat st;
    int fd;
    char *dev_name = "/dev/video0";
    bool isPass = true;

    if (-1 == stat(dev_name, &st)) {
            fprintf(stderr, "Cannot identify '%s': %d, %s\n",
                     dev_name, errno, strerror(errno));
            isPass = false;
            goto END;
    }

    if (!S_ISCHR(st.st_mode)) {
            fprintf(stderr, "%s is no device\n", dev_name);
            isPass = false;
            goto END;
    }

    //fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
    fd = open(dev_name, O_RDWR /* required */, 0);

    if (-1 == fd) {
            fprintf(stderr, "Cannot open '%s': %d, %s\n",
                     dev_name, errno, strerror(errno));
            isPass = false;
            goto END;
    }

    *fileDescriptor = fd;

END:
    return isPass;
}

/******************************************************************************
 *
 * close_device
 *
 *
 * Description: frees the connection to the video device
 *
 * Arguments:   fileDescriptor - pointer to the file descriptor of the opened
 *                               video device
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool close_device( int *fileDescriptor )
{
        if ( -1 == close( *fileDescriptor ) )
        {
            return false;
        }

        *fileDescriptor = -1;
        return true;
}

#endif // #ifndef _V4L2_LIBRARY_C
