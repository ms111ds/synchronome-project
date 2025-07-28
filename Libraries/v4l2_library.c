#include "v4l2_library.h"

#ifndef _V4L2_LIBRARY_C
#define _V4L2_LIBRARY_C


/******************************************************************************
 *
 * init_v4l2_state
 *
 *
 * Description: Initialize the v4l2 state to a known state. Should be done
 *              before opening the video device.
 *
 * Arguments:   state - v4l2 state to be initialized.
 *
 * Return:      N/A.
 *
 *****************************************************************************/
void init_v4l2_state( struct v4l2_state *state )
{
    state->fileDescriptor = -1;
    memset( state->deviceName, '\0', sizeof(state->deviceName) );
}



/******************************************************************************
 *
 * open_device
 *
 *
 * Description: opens the video device for use by this program.
 *
 * Arguments:   deviceName - null terminated string containing the name of the
 *                           video device.
 *
 *              state - v4l2 state to record opened device information.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool open_device( char *deviceName, struct v4l2_state *state )
{
    struct stat st;
    int fd;
    //char *dev_name = "/dev/video0";
    bool isPass = true;

    if ( strlen(deviceName) + 1 > sizeof(state->deviceName) )
    {
        fprintf( stderr,
                 "device name '%s' is too long (max %lu)\n",
                 deviceName,
                 sizeof(state->deviceName) - 1 );
        isPass = false;
        goto END;
    }

    if ( -1 == stat( deviceName, &st ) )
    {
        fprintf( stderr,
                 "Cannot identify '%s': %d, %s\n",
                 deviceName,
                 errno,
                 strerror(errno) );
        isPass = false;
        goto END;
    }

    if ( !S_ISCHR( st.st_mode ) )
    {
        fprintf(stderr, "%s is no device\n", deviceName );
        isPass = false;
        goto END;
    }

    //fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);
    fd = open( deviceName, O_RDWR /* required */, 0);

    if ( -1 == fd )
    {
        fprintf( stderr,
                 "Cannot open '%s': %d, %s\n",
                 deviceName,
                 errno,
                 strerror(errno) );
        isPass = false;
        goto END;
    }

    state->fileDescriptor = fd;
    strncpy( state->deviceName, deviceName, sizeof(state->deviceName) );

END:
    return isPass;
}

/******************************************************************************
 *
 * close_device
 *
 *
 * Description: Closes the connection to the video device.
 *
 * Arguments:   state - v4l2 state with the information of an open video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool close_device( struct v4l2_state *state )
{
        if ( -1 == close( state->fileDescriptor ) )
        {
            return false;
        }

        state->fileDescriptor = -1;
        return true;
}

#endif // #ifndef _V4L2_LIBRARY_C
