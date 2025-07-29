#include "v4l2_library.h"

#ifndef _V4L2_LIBRARY_C
#define _V4L2_LIBRARY_C

#define NUM_BUFFERS 4



//static void errno_exit(const char *s)
//{
//        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
//        exit(EXIT_FAILURE);
//}

static void errno_print(const char *s)
{
        fprintf(stderr, "%s error %d, %s\n", s, errno, strerror(errno));
}

static int xioctl(int fh, int request, void *arg)
{
        int r;

        do 
        {
            r = ioctl(fh, request, arg);

        } while (-1 == r && EINTR == errno);

        return r;
}

/******************************************************************************
 *
 * init_mmap
 *
 *
 * Description: Initializes video capture buffers. Buffers will be of the
 *              memory mapped type, i.e. the application will recieve pointers
 *              to directly access the camera's kernel space buffers. The camera
 *              directly writes to this buffer and the application can then read
 *              it. The total number of buffers will be determined by the video
 *              capture device after a request for NUM_BUFFERS buffers is
 *              sent by the application.
 *
 * Arguments:   state - v4l2 state with the information of an open video device. 
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool init_mmap( struct v4l2_state *state )
{
    struct v4l2_requestbuffers req;
    struct buffer *buffers = NULL;
    int i;
    bool isSuccess = false;
    int numMaps = 0;

    // Request a number (NUM_BUFFERS) of buffers within the video capture
    // device's memory for use by the application to read images captured
    // by the device.
    memset(&req, '\0', sizeof(req) );

    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if ( -1 == xioctl( state->fileDescriptor , VIDIOC_REQBUFS, &req ) ) 
    {
        if (EINVAL == errno) 
        {
            fprintf(stderr,
                    "%s does not support memory mapping\n",
                    state->deviceName );
        }
        else 
        {
            fprintf(stderr, "VIDIOC_REQBUFS\n" );
        }
        
        goto END;
    }

    // If the device allocates too few buffers for the application, exit.
    if ( req.count < 2 ) 
    {
        fprintf(stderr,
                "Insufficient buffer memory on %s\n",
                state->deviceName );
        goto END;
    }

    //buffers = calloc(req.count, sizeof(*buffers));
    buffers = calloc( req.count, sizeof(struct buffer) );

    if ( buffers == NULL ) 
    {
        errno_print( "Out of memory\n" );
        goto END;
    }

    for ( i = 0; i < req.count; ++i )
    {
        struct v4l2_buffer buf;

        memset(&buf, '\0', sizeof(buf) );

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = i;

        if ( -1 == xioctl( state->fileDescriptor, VIDIOC_QUERYBUF, &buf ) )
        {
            errno_print( "VIDIOC_QUERYBUF" );
            goto END;
        }

        // Map the device's buffers to the application.
        buffers[i].length = buf.length;
        buffers[i].start =  mmap(NULL /* start anywhere */,
                            buf.length,
                            PROT_READ | PROT_WRITE /* required */,
                            MAP_SHARED /* recommended */,
                            state->fileDescriptor,
                            buf.m.offset);

        if ( MAP_FAILED == buffers[i].start )
        {
            errno_print( "mmap" );
            goto END;
        }

        numMaps++;
    }


    state->numBuffers = req.count;
    state->bufferList = buffers;
    isSuccess = true;

END:

    if ( isSuccess == false )
    {
        for ( i = 0; i < numMaps; i++ )
        {
            munmap( buffers[i].start, buffers[i].length);
        }
    
        req.count = 0;
        xioctl( state->fileDescriptor , VIDIOC_REQBUFS, &req );
        
        if ( buffers != NULL )
        {
            free( buffers );
        }
    }
        
    return isSuccess;
}


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
    memset( state, '\0', sizeof(*state) );
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



/******************************************************************************
 *
 * init_device
 *
 *
 * Description: Sets up video format (e.g. image resolution ) and buffers to
 *              be used by the video capture device.
 *
 * Arguments:   ioMethod - input/output method to be used for communication
 *                         between this program in the video device.
 *
 *              numHorizontalPixels - horizontal length of images to sent by
 *                                    the camera. Let the camera decide by
 *                                    setting this and numVerticalPixels to
 *                                    zero.
 *
 *              numVerticalPixels - vertical length of images to sent by
 *                                  the camera. Let the camera decide by
 *                                  setting this and numHorizontalPixels to
 *                                  zero.
 *
 * Arguments:   state - v4l2 state with the information of an open video device
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool init_device( enum io_method ioMethod,
                  unsigned int numHorizontalPixels,
                  unsigned int numVerticalPixels,
                  struct v4l2_state *state )
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format formatData;
    unsigned int min;
    bool isSuccess = true;

    // query device capabilities to see if it is Video For Linux (V4L2)
    // compatible.
    if ( -1 == xioctl( state->fileDescriptor, VIDIOC_QUERYCAP, &cap ) )
    {
        if ( EINVAL == errno )
        {
            fprintf( stderr, "%s is no V4L2 device\n", state->deviceName );
        }
        else
        {
            fprintf( stderr, "VIDIOC_QUERYCAP failed\n" );
        }
        isSuccess = false;
        goto END;
    }

    // check to see if V4L2 device can capture video.
    if ( !( cap.capabilities & V4L2_CAP_VIDEO_CAPTURE ) )
    {
        fprintf(stderr,
                "%s is no video capture device\n",
                state->deviceName );
        isSuccess = false;
        goto END;
    }

    // Check if the desired I/O method is available.
    switch ( ioMethod )
    {
        case IO_METHOD_READ:
            if ( !( cap.capabilities & V4L2_CAP_READWRITE ) )
            {
                fprintf(stderr,
                        "%s does not support read i/o\n",
                        state->deviceName );
        	isSuccess = false;
        	goto END;
            }
            break;

        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            if (!(cap.capabilities & V4L2_CAP_STREAMING))
            {
                fprintf(stderr,
                        "%s does not support streaming i/o\n",
                        state->deviceName );
               	isSuccess = false;
        	goto END;
            }
            break;
    }


    /* Select video input, video standard and tune here. */


    // Check to see if image cropping is available.
    memset(&cropcap, '\0', sizeof(cropcap) );

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if ( 0 == xioctl( state->fileDescriptor, VIDIOC_CROPCAP, &cropcap ) )
    {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        if ( -1 == xioctl( state->fileDescriptor, VIDIOC_S_CROP, &crop ) )
        {
            switch (errno)
            {
                case EINVAL:
                    /* Cropping not supported. */
                    break;
                default:
                    /* Errors ignored. */
                    break;
            }
        }

    }
    else
    {
        /* Errors ignored. */
    }


    // Select or force video format.
    memset(&formatData, '\0', sizeof(formatData) );
    
    
    formatData.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if ( ( numHorizontalPixels > 0 ) && ( numVerticalPixels > 0 ) )
    {
        printf("FORCING FORMAT ON CAMERA\n");
        formatData.fmt.pix.width       = numHorizontalPixels;
        formatData.fmt.pix.height      = numVerticalPixels;

        // Specify the Pixel Coding Formate here

        // This one work for Logitech C200
        formatData.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_VYUY;

        // Would be nice if camera supported
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
        //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;

        //fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
        formatData.fmt.pix.field       = V4L2_FIELD_NONE;

        printf( "pixelformat 1: %x\n", formatData.fmt.pix.pixelformat );

        if ( -1 == xioctl( state->fileDescriptor, VIDIOC_S_FMT, &formatData ) )
        {
            isSuccess = false;
            errno_print( "VIDIOC_S_FMT" );
            goto END;
        }

        printf( "pixelformat 2: %x\n",formatData.fmt.pix.pixelformat );

        /* Note VIDIOC_S_FMT may change width and height. */
    }
    else
    {
        printf("LET CAMERA DECIDE FORMAT\n");
        /* Preserve original settings as set by v4l2-ctl for example */
        if ( -1 == xioctl( state->fileDescriptor, VIDIOC_G_FMT, &formatData ) )
        {
            isSuccess = false;
            errno_print( "VIDIOC_G_FMT" );
            goto END;
        }
    }

    /* Buggy driver paranoia. */
    min = formatData.fmt.pix.width * 2; // YUYV pixel info encoded in 2 bytes
    if ( formatData.fmt.pix.bytesperline < min )
    {
        formatData.fmt.pix.bytesperline = min;
    }
    
    min = formatData.fmt.pix.bytesperline * formatData.fmt.pix.height;
    if ( formatData.fmt.pix.sizeimage < min )
    {
        formatData.fmt.pix.sizeimage = min;
    }

    // Initialize selected I/O method
    switch ( ioMethod )
    {
        case IO_METHOD_READ:
            //init_read(fmt.fmt.pix.sizeimage);
            isSuccess = false;
            printf( "read()/write() I/O currently not supported\n" );
            goto END;
            break;

        case IO_METHOD_MMAP:
            init_mmap( state );
            break;

        case IO_METHOD_USERPTR:
            //init_userp(fmt.fmt.pix.sizeimage);
            isSuccess = false;
            printf( "user pointer I/O currently not supported\n" );
            goto END;
            break;
    }

    state->ioMethod = ioMethod;
    memcpy( &(state->formatData), &formatData, sizeof(struct v4l2_format) );
    
END:

    return isSuccess;
}

/******************************************************************************
 *
 * uninit_device
 *
 *
 * Description: Free the buffers used by the video capture device to
 *              communicate with this application.
 *
 * Arguments:   state - v4l2 state with the information of an open video device.
 *
 * Return:      true if nothing went wrong during the uninitialization. false
 *              something did not ininitialize as it should.
 *
 *****************************************************************************/
bool uninit_device( struct v4l2_state *state )
{
    unsigned int i;
    struct v4l2_requestbuffers req;
    bool isOk = true;

    switch ( state->ioMethod )
    {
        case IO_METHOD_READ:
            //free(buffers[0].start);
            printf( "read()/write() I/O currently not supported\n" );
            break;
        
        case IO_METHOD_MMAP:
            for ( i = 0; i < state->numBuffers; ++i )
            {
                if ( -1 == munmap(state->bufferList[i].start,
                                  state->bufferList[i].length) )
                {
                    errno_print("munmap");
                    isOk = false;
                }
            }
            req.count = 0;
    	    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    	    req.memory = V4L2_MEMORY_MMAP;
            if ( -1 == xioctl( state->fileDescriptor , VIDIOC_REQBUFS, &req ) )
            {
                errno_print("ioctl VIDIOC_REQBUFS");
                isOk = false;
            }
            
            break;
        
        case IO_METHOD_USERPTR:
            //free(buffers[0].start);
            printf( "user pointer I/O currently not supported\n" );
            break;
    }

    free( state->bufferList );
    state->numBuffers = 0;
    state->bufferList = NULL;
    memset( &(state->formatData), '\0', sizeof(state->formatData) );

    return isOk;
}



/******************************************************************************
 *
 * start_capturing
 *
 *
 * Description: Start the image capture process when using memory mapping (MMAP)
 *              or user pointers.
 *
 * Arguments:   state - v4l2 state with the information of an open video device. 
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool start_capturing( struct v4l2_state *state )
{
    //unsigned int i;
    enum v4l2_buf_type type;
    bool isOk = true;

    switch ( state->ioMethod ) 
    {
        case IO_METHOD_READ:
            /* Nothing to do. */
            printf( "read()/write() I/O currently not supported\n" );
            isOk = false;
            break;
        
        case IO_METHOD_MMAP:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            if ( -1 == xioctl( state->fileDescriptor,
                               VIDIOC_STREAMON,
                               &type ) )
            {
                errno_print( "VIDIOC_STREAMON" );
                isOk = false;
            }

            break;
        
        case IO_METHOD_USERPTR:
            //type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            //if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
            //        errno_exit("VIDIOC_STREAMON");
            printf( "user pointer I/O currently not supported\n" );
            isOk = false;
            break;
    }

    return isOk;
}

/******************************************************************************
 *
 * stop_capturing
 *
 *
 * Description: Stop the image capture process when using memory mapping (MMAP)
 *              or user pointers.
 *
 * Arguments:   state - v4l2 state with the information of an open video device. 
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool stop_capturing( struct v4l2_state *state )
{
    enum v4l2_buf_type type;
    bool isPass = true;

    switch ( state->ioMethod )
    {
        case IO_METHOD_READ:
            /* Nothing to do. */
            printf( "read()/write() I/O currently not supported\n" );
            isPass = false;
            break;
        
        case IO_METHOD_MMAP:
        case IO_METHOD_USERPTR:
            type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            
            if ( state->ioMethod == IO_METHOD_USERPTR )
            {
                printf( "user pointer I/O currently not supported\n" );
                isPass = false;
            }
            else if ( -1 == xioctl( state->fileDescriptor,
                                    VIDIOC_STREAMOFF,
                                    &type ) )
            {
                errno_print("VIDIOC_STREAMOFF");
                isPass = false;
            }
            break;
    }

    return isPass;
}

#endif // #ifndef _V4L2_LIBRARY_C
