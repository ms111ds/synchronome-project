#include "test_services.h"

#ifndef _TEST_SRV_C
#define _TEST_SRV_C

#define CORE_0 0
#define CORE_1 1
#define CORE_2 2
#define CORE_3 3

struct thread_args
{
    struct v4l2_state *state;
};

pthread_attr_t     attrTest;
pthread_t          threadTest;
struct sched_param paramsTest;

pthread_attr_t     attrStarter;
pthread_t          threadStarter;
struct sched_param paramsStarter;

sem_t semImageLoader;
sem_t semImageSelector;
sem_t semImageProcessor;
sem_t semImageWriter;
pthread_mutex_t mutexTest;
unsigned int sequencePeriods = 30;
unsigned int remainingSequencePeriods;

double imageLoaderCumulativeTime;
double imageLoaderMaxTime;
double imageLoaderMinTime;
double imageSelectorCumulativeTime;
double imageSelectorMaxTime;
double imageSelectorMinTime;
double imageProcessorCumulativeTime;
double imageProcessorMaxTime;
double imageProcessorMinTime;
double imageWriterCumulativeTime;
double imageWriterMaxTime;
double imageWriterMinTime;


static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int schedulingPolicy,
                                   int priority,
                                   int affinity );
static void *starter( void *threadp );
void sequencer( int id );
static void *service_test( void *threadp );
static bool loadImage( struct v4l2_state *state );
static bool selectImage ( struct v4l2_state *state );
static bool processImage( struct v4l2_state *state );
static bool writeImage ( struct v4l2_state *state, unsigned int tag );
static bool add_ppm_header( char *buf,
                            unsigned int bufLen,
                            unsigned int seconds,
                            unsigned int microSeconds,
                            unsigned int horizontalResolution,
                            unsigned int verticalResolution,
                            unsigned int *stampLen );
static int convert_yuyv_image_to_rgb( char *yuyvBufIn,
                                       int yuyvBufLen,
                                       char *rgbBufOut );
static void dump_ppm(const void *p, int size, unsigned int tag );
static void yuv2rgb(int y,
                    int u,
                    int v,
                    unsigned char *r,
                    unsigned char *g,
                    unsigned char *b);
static void print_scheduler(void);

/******************************************************************************
 *
 * run_test_services
 *
 *
 * Description: Starts the starter thread (real-time thread).
 *
 * Arguments:   state - v4l2 state with the information of an open video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool run_test_services( struct v4l2_state *state )
{
    int threadMaxPriority;
    bool isPass = false;
    int rv;
    struct thread_args serviceArgs;
    
    //if ( init_buffer_status_array() != 0 )
    //{
    //    exit(EXIT_FAILURE);
    //}

    // Unlink is needed in case the
    // the message queues already exist. This call will delete these already
    // existing ones.
    //mq_unlink( TO_PROCESSING_MQ );
    //mq_unlink( FROM_PROCESSING_MQ );

    // Set thread attributes of the image loading service, the image processing
    // service and the scheduler.
    threadMaxPriority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( threadMaxPriority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }

    // set "test" service thread attributes
    isPass = set_thread_attributes( &attrStarter,
                                    &paramsStarter,
                                    MY_SCHED_POLICY,
                                    threadMaxPriority,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: starter service\n");
        goto END;
    }

    serviceArgs.state = state;
    // Start the launger and wait for it to join back.
    // Starts the test service and the scheduler
    rv = pthread_create( &threadStarter, // pointer to thread descriptor
                         &attrStarter,   // use specific attributes
                         starter,        // thread function entry point
                         (void *)&serviceArgs ); // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating starter service\n" );
        goto END;
    }

    rv = pthread_join( threadStarter, NULL );
    if ( rv < 0)
    {
        errno_print( "error joining with starter service\n");
        goto END;
    }

END:

    return isPass;
}

/******************************************************************************
 *
 * set_thread_attributes
 *
 *
 * Description: Sets up scheduling policy, priority, and CPU affinity for
 *              the passed in pthread attribute struct.
 *
 * Arguments:   attr (OUT):    pthread attribute struct that will be set up.
 *
 *              params (OUT):  scheduling parameters struct that will be set up.
 *                                      service.
 *
 *              schedulingPolicy (IN): scheduling policy to set up in attr.
 *
 *              priority (IN): the priority to set up in attr and params.
 *
 *              affinity (IN): the CPU affinity to set up in attr.
 *
 * Return:      true if successful. false otherwise.
 *
 *****************************************************************************/
static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int schedulingPolicy,
                                   int priority,
                                   int affinity )
{
    int rc = 0;
    cpu_set_t thread_cpu;
    int cpu_idx;
    bool isPass = false;

    // set scheduling policy
    rc = pthread_attr_init( attr );
    if ( rc != 0 )
    {
        errno_print( "pthread_attr_init" );
        goto END;
    }

    rc = pthread_attr_setinheritsched( attr, PTHREAD_EXPLICIT_SCHED);
    if ( rc != 0 )
    {
        errno_print( "pthread_attr_setinheritsched" );
        goto END;
    }

    rc = pthread_attr_setschedpolicy( attr, schedulingPolicy );
    if ( rc != 0 )
    {
        errno_print( "pthread_attr_setschedpolicy" );
        goto END;
    }

    // set priority
    params->sched_priority = priority;
    rc = pthread_attr_setschedparam( attr, params );
    if ( rc != 0 )
    {
        errno_print( "pthread_attr_setschedparam" );
        goto END;
    }

    // set processor affinity
    CPU_ZERO(&thread_cpu);
    cpu_idx = affinity ;
    CPU_SET(cpu_idx, &thread_cpu);
    rc = pthread_attr_setaffinity_np( attr, sizeof(thread_cpu), &thread_cpu );
    if ( rc != 0 )
    {
        errno_print( "pthread_attr_setaffinity_np" );
        goto END;
    }

    isPass = true;

END:
    return isPass;  
}

/******************************************************************************
 *
 * starter
 *
 *
 * Description: This is the starter thread. It is a real-time thread which
 *              initializes mutexes and semaphores needed by the sequencer and
 *              the test service. It then starts the test the service and the
 *              sequencer. The sequencer timer is also started here.
 *
 * Arguments:   threadp (IN): arguments to pass into created threads.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *starter( void *threadp )
{
    int rv;
    bool isPass;
    int threadMaxPriority;
    timer_t sequencerTimer;
    struct itimerspec intervalTime = { {1,0}, { 1,0 } };
    struct itimerspec oldIntervalTime = { {1,0}, { 1,0 } };
    int flags = 0;
    bool isOkSemL = false;
    bool isOkSemS = false;
    bool isOkSemP = false;
    bool isOkSemW = false;
    bool isMutCreated = false;

    printf( "starter " );
    print_scheduler( );

    // Initialize semaphores
    if ( sem_init( &semImageLoader, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkSemL = true;

    if ( sem_init( &semImageSelector, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkSemS = true;

    if ( sem_init( &semImageProcessor, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkSemP = true;

    if ( sem_init( &semImageWriter, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkSemW = true;

    // Initialize mutexes
    if ( pthread_mutex_init(&mutexTest, NULL) != 0 )
    {
        errno_print( "mutex init" );
        goto END;
    }
    isMutCreated = true;

    // Set thread attributes for the service that will carry out all tests.
    threadMaxPriority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( threadMaxPriority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }

    isPass = set_thread_attributes( &attrTest,
                                    &paramsTest,
                                    MY_SCHED_POLICY,
                                    threadMaxPriority - 1,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: starter service\n");
        goto END;
    }


    // Create the testing service thread.
    rv =  pthread_create(&threadTest,  // pointer to thread descriptor
                         &attrTest,    // use specific attributes
                         service_test, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating test service\n" );
        goto END;
    }

    // Set up a timer which will trigger the execution of the sequencer
    // whenever it counts to zero.
    /* set up to signal SIGALRM if timer expires */
    remainingSequencePeriods = sequencePeriods;
    timer_create( CLOCK_REALTIME, NULL, &sequencerTimer );
    signal(SIGALRM, (void(*)()) sequencer);

    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = 100000000; // refill timer with this
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = 1; // initial value. start ASAP.

    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );


    // Wait for the test service thread to complete
    rv = pthread_join( threadTest, NULL );
    if ( rv < 0)
    {
        fprintf(stderr, "error joining with test service\n");
    }

    // Disable the timer
    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = 0;
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = 0;
    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );

END:

    if ( isOkSemL == true ) sem_destroy( &semImageLoader );
    if ( isOkSemS == true ) sem_destroy( &semImageSelector );
    if ( isOkSemP == true ) sem_destroy( &semImageProcessor );
    if ( isOkSemW == true ) sem_destroy( &semImageWriter );
    if ( isMutCreated == true ) pthread_mutex_destroy( &mutexTest );
    return (void *)NULL;
}

/******************************************************************************
 *
 * sequencer
 *
 *
 * Description: The sequencer function. Post semaphores whenever each service
 *              should run.
 *
 * Arguments:   id (IN): not used.
 *
 * Return:      N/A
 *
 *****************************************************************************/
void sequencer(int id)
{
    //printf( "in the SE-SE-SEQUENCER!!\n" );
    pthread_mutex_lock( &mutexTest );
    if ( remainingSequencePeriods > 0 ) remainingSequencePeriods--;
    pthread_mutex_unlock( &mutexTest );
    sem_post( &semImageLoader );
    sem_post( &semImageSelector );
    sem_post( &semImageProcessor );
    sem_post( &semImageWriter );
}


/******************************************************************************
 *
 * service_test
 *
 *
 * Description: runs the tests.
 *
 * Arguments:   threadp (IN): Contains the parameters to be used by the tests.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *service_test( void *threadp )
{
    bool isFinal = false;
    struct timespec startTime;
    struct timespec endTime;
    double deltaTimeUs;
    static int curRun = 0;
    struct v4l2_state *state = ( (struct thread_args *)threadp )->state;


    // Initialize values for performance measurments
    imageLoaderCumulativeTime = 0.0;
    imageLoaderMaxTime = DBL_MIN;
    imageLoaderMinTime = DBL_MAX;
    imageSelectorCumulativeTime = 0.0;
    imageSelectorMaxTime = DBL_MIN;
    imageSelectorMinTime = DBL_MAX;
    imageProcessorCumulativeTime = 0.0;
    imageProcessorMaxTime = DBL_MIN;
    imageProcessorMinTime = DBL_MAX;
    imageWriterCumulativeTime = 0.0;
    imageWriterMaxTime = DBL_MIN;
    imageWriterMinTime = DBL_MAX;

    // fill up one out of 2 image buffers. Once we have 2 image buffers, we can
    // start comparing images.
    loadImage( state );

    while ( true )
    {
        curRun++;

        // IMAGE LOADER CODE
        sem_wait( &semImageLoader );
        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );

        loadImage( state ); // ACTUAL TEST HERE

        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );
        imageLoaderCumulativeTime += deltaTimeUs;
        if ( deltaTimeUs > imageLoaderMaxTime )
        {
            imageLoaderMaxTime = deltaTimeUs;
        }
        if ( deltaTimeUs < imageLoaderMinTime )
        {
            imageLoaderMinTime = deltaTimeUs;
        }
        syslog(LOG_CRIT,
               "synchronome service test round %i: %9.3lf us (image load)\n",
               curRun,
               deltaTimeUs );


        // IMAGE SELECTOR CODE
        sem_wait( &semImageSelector );
        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );

        selectImage( state ); // ACTUAL TEST HERE

        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );
        imageSelectorCumulativeTime += deltaTimeUs;
        if ( deltaTimeUs > imageSelectorMaxTime )
        {
            imageSelectorMaxTime = deltaTimeUs;
        }
        if ( deltaTimeUs < imageSelectorMinTime )
        {
            imageSelectorMinTime = deltaTimeUs;
        }
        syslog(LOG_CRIT,
               "synchronome service test round %i: %9.3lf us (image select)\n",
               curRun,
               deltaTimeUs );


        // IMAGE PROCESSOR CODE
        sem_wait( &semImageProcessor );
        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );

        processImage( state ); // ACTUAL TEST HERE

        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );
        imageProcessorCumulativeTime += deltaTimeUs;
        if ( deltaTimeUs > imageProcessorMaxTime )
        {
            imageProcessorMaxTime = deltaTimeUs;
        }
        if ( deltaTimeUs < imageProcessorMinTime )
        {
            imageProcessorMinTime = deltaTimeUs;
        }
        syslog(LOG_CRIT,
               "synchronome service test round %i: %9.3lf us (image process)\n",
               curRun,
               deltaTimeUs );


        

        // IMAGE WRITER CODE
        sem_wait( &semImageWriter );
        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );

        writeImage( state, curRun ); // ACTUAL TEST HERE

        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );
        imageWriterCumulativeTime += deltaTimeUs;
        if ( deltaTimeUs > imageWriterMaxTime )
        {
            imageWriterMaxTime = deltaTimeUs;
        }
        if ( deltaTimeUs < imageWriterMinTime )
        {
            imageWriterMinTime = deltaTimeUs;
        }
        syslog(LOG_CRIT,
               "synchronome service test round %i: %9.3lf us (image process)\n",
               curRun,
               deltaTimeUs );


        // Check if service should end
        pthread_mutex_lock( &mutexTest );
        if ( remainingSequencePeriods == 0 ) isFinal = true;
        pthread_mutex_unlock( &mutexTest );
        if ( isFinal == true ) break;
    }

    // Print summary of performance measurements
    syslog(LOG_CRIT,
           "image load summary (us) max: %9.3lf, min: %9.3lf, avg: %9.3lf \n",
           imageLoaderMaxTime,
           imageLoaderMinTime,
           imageLoaderCumulativeTime / (double)curRun );

    syslog(LOG_CRIT,
           "image select summary (us) max: %9.3lf, min: %9.3lf, avg: %9.3lf \n",
           imageSelectorMaxTime,
           imageSelectorMinTime,
           imageSelectorCumulativeTime / (double)curRun );

    syslog(LOG_CRIT,
           "image process summary (us) max: %9.3lf, min: %9.3lf, avg: %9.3lf \n",
           imageProcessorMaxTime,
           imageProcessorMinTime,
           imageProcessorCumulativeTime / (double)curRun );

    syslog(LOG_CRIT,
           "image write summary (us) max: %9.3lf, min: %9.3lf, avg: %9.3lf \n",
           imageWriterMaxTime,
           imageWriterMinTime,
           imageWriterCumulativeTime / (double)curRun );

    return (void *)NULL;
}


/******************************************************************************
 *
 * loadImage
 *
 *
 * Description: Test to see how long it takes for the camera to produce an
 *              image. First QUEUES a buffer so the camera knows a buffer is
 *              ready to write in. Then it DEQUEUES the buffer to have the
 *              application wait for the image to be loaded by the camera.
 *              This test assumes mmap (memory mapped) buffer communication
 *              between the application and the camera.
 *
 * Arguments:   state (IN/OUT): v4l2 state with the information of an open
 *              video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool loadImage( struct v4l2_state *state )
{
    bool isPass = true;
    int readyBufIndex;


    // Tell the camera that a buffer ready to write in
    isPass = queue_stream_bufs( state->curBufIndex,
                                state );

    // Wait for the camera to write into the buffer 
    isPass = read_frame_stream( &readyBufIndex, state );

    if ( state->curBufIndex != (unsigned)readyBufIndex )
    {
        printf( "load image error: current buffer = %u, dequeued buffer %i",
                state->curBufIndex,
                readyBufIndex );
    }

    // go to the next buffer. Since we only use 2 buffers, the next will be
    // either buffer 0 or buffer 1.
    state->curBufIndex = ( state->curBufIndex + 1 ) & 0x1;

    return isPass;
}


/******************************************************************************
 *
 * selectImage
 *
 *
 * Description: A simple test to see if the last two images created by the
 *              camera are the same.
 *
 * Arguments:   state (IN/OUT): v4l2 state with the information of an open
 *              video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool selectImage( struct v4l2_state *state )
{
    int i = 0;

    // Ensure lengths of the images are the same
    if ( state->bufferList[0].length != state->bufferList[1].length )
    {
        printf( "image length discrepancy buf0 = %lu, buf1 = %lu\n",
                state->bufferList[0].length,
                state->bufferList[1].length );
        return false;
    }

    // Check to see if the images are equivalent
    if ( memcmp( state->bufferList[0].start ,
                 state->bufferList[1].start,
                 state->bufferList[0].length ) != 0 )
    {
        i++; // This has no purpose other than to get the compiler to not
             // throw a warning that the memcmp isn't doing anything.
    }
    
    return true;
}


/******************************************************************************
 *
 * processImage
 *
 *
 * Description: Extracts the YUYV image out of the camera's memory mapped area
 *              and saves it in application space as an RGB ppm formatted image.
 *
 * Arguments:   state (IN/OUT): v4l2 state with the information of an open
 *              video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool processImage( struct v4l2_state *state )
{

    struct timespec curTime;
    unsigned int stampLen;
    // latest image always stored on the buffer that isn't the current one
    // to load an image to.
    int latestImageIndex = ( state->curBufIndex + 1 ) & 0x1;
    bool isPass = false;
    int outFrameSize;

    // Add the timestamp to the image
    clock_gettime( CLOCK_MONOTONIC_RAW, &curTime );
    isPass = add_ppm_header( state->outBuffer,
                             state->outBufferSize,
                             (unsigned int)curTime.tv_sec,
                             (unsigned int)curTime.tv_nsec / 1000,
                             (unsigned int)state->formatData.fmt.pix.width,
                             (unsigned int)state->formatData.fmt.pix.height,
                             &stampLen );
    if ( isPass == false )
    {
        goto END;
    }

    // Transfer the image out of kernel space in RGB format
    outFrameSize = convert_yuyv_image_to_rgb( state->bufferList[latestImageIndex].start,
                                              state->bufferList[latestImageIndex].length,
                                              state->outBuffer + stampLen );

    state->outDataSize = stampLen + outFrameSize;
    memcpy( &(state->outDataTimeStamp),
            &curTime,
            sizeof(state->outDataTimeStamp) );
    isPass = true;

    //printf( "out sizes: header %u, frame %lu, total %u\n",
    //        stampLen,
    //        state->bufferList[latestImageIndex].length,
    //        state->outDataSize );


END:
    return true;
}

/******************************************************************************
 *
 * writeImage
 *
 *
 * Description: Saves the ppm file to disk.
 *
 * Arguments:   state (IN/OUT): v4l2 state with the information of an open
 *              video device.
 *
 *              tag (IN): An number to attach to the written file's name.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool writeImage ( struct v4l2_state *state, unsigned int tag )
{

    dump_ppm( state->outBuffer, state->outDataSize, tag );
    return true;
}


/******************************************************************************
 *
 * add_ppm_header
 *
 *
 * Description: Inserts a custom ppm header (stamp) into a buffer.
 *
 * Arguments:   buf (OUT): buffer to write the header to.
 *
 *              bufLen (IN): size of the buffer.
 *
 *              seconds (IN): a value for seconds in the timestame.
 *
 *              microsecods (IN): a value for microseconds in the timestamp.
 *
 *              horizontalResolution (IN): Horizontal resolution of the image.
 *
 *              verticalResolution (IN): Vertical resolution of the image.
 *
 *              stampLen (OUT): Length of the header will be output here.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
static bool add_ppm_header( char *buf,
                            unsigned int bufLen,
                            unsigned int seconds,
                            unsigned int microSeconds,
                            unsigned int horizontalResolution,
                            unsigned int verticalResolution,
                            unsigned int *stampLen )
{
    unsigned int i = 0;
    unsigned int resArr[2] = { horizontalResolution, verticalResolution };
    unsigned int calculatedStampSize = 0;
    unsigned int resolutionStrLen = 0;
    unsigned int quotient;

    for( i = 0; i < sizeof(resArr)/sizeof(resArr[0]); i++ )
    {
        quotient = resArr[i];
        do
        {
            quotient /= 10;
            resolutionStrLen++;
        } while ( quotient > 0 );
    }

    //char ppm_header[]="P6\n#9999999999 sec 9999999999 usec \n"HRES_STR" "VRES_STR"\n255\n"
    calculatedStampSize = 3 + // P6\n
                          33 + // #9999999999 sec 9999999999 usec \n
                          resolutionStrLen + 2 + //HRES_STR VRES_STR\n
                          4; // 255\n

    if ( bufLen < calculatedStampSize )
    {
        printf( "add timestamp failed, buflen: %u, calc stamp size %u\n",
                bufLen,
                calculatedStampSize );
        return false;
    }
    
    sprintf( buf,
             "P6\n#%010u sec %010u usec \n%u %u\n255\n",
             seconds,
             microSeconds,
             horizontalResolution,
             verticalResolution );

    *stampLen = calculatedStampSize;

    return true;
}



/******************************************************************************
 *
 * convert_yuyv_image_to_rgb
 *
 *
 * Description: Converts an entire image in YUYV format into RGB format.
 *
 * Arguments:   yuyvBufIn (IN): buffer holding an image in YUYV format.
 *
 *              yuyvBufLen (IN): Size, in bytes, of the YUYV image.
 *
 *              rgbBufOut (OUT): Buffer to place the converted RGB image.
 *
 * Return:      The length, in bytes, of the new RGB image.
 *
 *****************************************************************************/
static int convert_yuyv_image_to_rgb( char *yuyvBufIn,
                                      int yuyvBufLen,
                                      char *rgbBufOut )
{
    int i;
    int newi;
    int y_temp;
    int y2_temp;
    int u_temp;
    int v_temp;
    unsigned char *pptr = (unsigned char *)yuyvBufIn;
    
    for( i = 0, newi = 0; i < yuyvBufLen; i = i + 4, newi = newi + 6)
    {
        y_temp = (int)pptr[i];
        u_temp = (int)pptr[i+1];
        y2_temp= (int)pptr[i+2];
        v_temp= (int)pptr[i+3];
        yuv2rgb( y_temp,
                 u_temp,
                 v_temp,
                 (unsigned char *)&rgbBufOut[newi],
                 (unsigned char *)&rgbBufOut[newi+1],
                 (unsigned char *)&rgbBufOut[newi+2] );
        yuv2rgb( y2_temp,
                 u_temp,
                 v_temp,
                 (unsigned char *)&rgbBufOut[newi+3],
                 (unsigned char *)&rgbBufOut[newi+4],
                 (unsigned char *)&rgbBufOut[newi+5] );
    }

    return newi;
}
    

/******************************************************************************
 *
 * dump_ppm
 *
 *
 * Description: Output image (frame) data as a colour ppm file.
 *
 * Arguments:   p (IN):    Pointer to image data.
 *
 *              size (IN): Size of image data in bytes.
 *
 *              tag (IN):  A number which will go into the filename of the
 *                         created file.
 *
 * Return:      N/A
 *
 *****************************************************************************/
static void dump_ppm(const void *p, int size, unsigned int tag )
{
    int written, total, dumpfd;
    char ppm_dumpname[]="test00000000.ppm";
   
    snprintf(&ppm_dumpname[4], 9, "%08d", tag);
    //strncat(&ppm_dumpname[12], ".ppm", 4);
    memcpy( &ppm_dumpname[12], ".ppm", 4 );
    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    total = 0;
    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    //printf("wrote %d bytes\n", total);

    close(dumpfd);
    
}

// This is probably the most acceptable conversion from camera YUYV to RGB
//
// Wikipedia has a good discussion on the details of various conversions and
// cites good references: http://en.wikipedia.org/wiki/YUV
//
// Also http://www.fourcc.org/yuv.php
//
// What's not clear without knowing more about the camera in question is how
// often U & V are sampled compared to Y.
//
// E.g. YUV444, which is equivalent to RGB, where both require 3 bytes for each
// pixel YUV422, which we assume here, where there are 2 bytes for each pixel,
// with two Y samples for one U & V, or as the name implies, 4Y and 2 UV pairs
// YUV420, where for every 4 Ys, there is a single UV pair, 1.5 bytes for each
// pixel or 36 bytes for 24 pixels
static void yuv2rgb(int y,
                    int u,
                    int v,
                    unsigned char *r,
                    unsigned char *g,
                    unsigned char *b)
{
   int r1, g1, b1;

   // replaces floating point coefficients
   int c = y-16, d = u - 128, e = v - 128;       

   // Conversion that avoids floating point
   r1 = (298 * c           + 409 * e + 128) >> 8;
   g1 = (298 * c - 100 * d - 208 * e + 128) >> 8;
   b1 = (298 * c + 516 * d           + 128) >> 8;

   // Computed values may need clipping.
   if (r1 > 255) r1 = 255;
   if (g1 > 255) g1 = 255;
   if (b1 > 255) b1 = 255;

   if (r1 < 0) r1 = 0;
   if (g1 < 0) g1 = 0;
   if (b1 < 0) b1 = 0;

   *r = r1 ;
   *g = g1 ;
   *b = b1 ;
}

/******************************************************************************
 *
 * print_scheduler
 *
 *
 * Description: Prints the scheduling policy of the calling thread.
 *
 * Arguments:   N/A
 *
 * Return:      N/A
 *
 *****************************************************************************/
static void print_scheduler( void )
{
   int schedType;

   schedType = sched_getscheduler( gettid() );

   switch(schedType)
   {
       case SCHED_FIFO:
           printf("Pthread Policy is SCHED_FIFO\n");
           break;
       case SCHED_OTHER:
           printf("Pthread Policy is SCHED_OTHER\n");
         break;
       case SCHED_RR:
           printf("Pthread Policy is SCHED_RR\n");
           break;
       //case SCHED_DEADLINE:
       //    printf("Pthread Policy is SCHED_DEADLINE\n");
       //    break;
       default:
           printf("Pthread Policy is UNKNOWN\n");
   }
}

#endif // #ifndef _TEST_SRV_C
