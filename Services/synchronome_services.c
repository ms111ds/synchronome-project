#include "test_services.h"

#ifndef _SYNCHRONOME_SRV_C
#define _SYNCHRONOME_SRV_C

#define CORE_0 0
#define CORE_1 1
#define CORE_2 2
#define CORE_3 3

#define TO_PROCESSING_MQ "/to_processing_mq"
#define TO_SELECTION_MQ "/to_selection_mq"

#define MILLI_TO_NANO 1000000
#define UNIT_TIME_MS 20
#define T_LOAD_SELECT 5

struct thread_args
{
    struct v4l2_state *state;
};

struct load_to_select_msg
{
    char *buf1;
    char *buf2;
    unsigned int bufLen;
};

struct load_to_select_msg
{
    char *buf1;
    char *buf2;
    unsigned int bufLen;
};

struct select_to_process_msg
{
    char *buf;
    unsigned int bufLen;
};

pthread_attr_t     attrLoad;
pthread_t          threadLoad;
struct sched_param paramsLoad;

pthread_attr_t     attrSelect;
pthread_t          threadSelect;
struct sched_param paramsSelect;

pthread_attr_t     attrProcessWrite;
pthread_t          threadProcessWrite;
struct sched_param paramsProcessWrite;

pthread_attr_t     attrStarter;
pthread_t          threadStarter;
struct sched_param paramsStarter;

sem_t semLoad;
sem_t semSelect;
pthread_mutex_t mutexSynchronome;

unsigned int sequencePeriods = 60;
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
char unameBuf[256];
unsigned int unameBufLen;


static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int schedulingPolicy,
                                   int priority,
                                   int affinity );
static void *starter( void *threadp );
void sequencer( int id );
static void *service_load_and_selec( void *threadp );
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
static void addCoolBorder( char *yuyvBufIn, int yuyvBufLen, int pixelsPerLine );
static bool init_message_queue( mqd_t *mqDescriptor, char *mqName );
static void print_scheduler(void);

/******************************************************************************
 *
 * run_synchronome
 *
 *
 * Description: Starts the starter thread (real-time thread).
 *
 * Arguments:   state - v4l2 state with the information of an open video device.
 *
 * Return:      true if successful, false otherwise.
 *
 *****************************************************************************/
bool run_synchronome( struct v4l2_state *state )
{
    int threadMaxPriority;
    bool isPass = false;
    int rv;
    struct thread_args serviceArgs;

    FILE* unameOutput = popen("uname -a", "r");
    if ( fgets( unameBuf, sizeof(unameBuf), unameOutput) == NULL )
    {
        errno_print( "uname -a output\n" );
        goto END;
    }
    pclose( unameOutput );
    unameBufLen = (unsigned int)strlen( unameBuf );
    

    // Delete the message queues if they already exists
    mq_unlink( TO_SELECTION_MQ );
    mq_unlink( TO_PROCESSING_MQ );

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

    // clean up message queue when finished
    mq_unlink( TO_SELECTION_MQ );
    mq_unlink( TO_PROCESSING_MQ );

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
    bool isOkLoadSem = false;
    bool isOkSelectSem = false;
    bool isMutAttrCreated = false;
    bool isMutCreated = false;
    pthread_mutexattr_t mutex_attributes;

    printf( "starter " );
    print_scheduler( );

    // Initialize semaphores
    if ( sem_init( &semLoad, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkLoadSem = true;

    if ( sem_init( &semSelect, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isOkSelectSem = true;



    // Initialize mutex with priority inheritance
    if ( pthread_mutexattr_init( &mutex_attributes ) != 0 )
    {
        errno_print( "mutex attributes init" );
        goto END;
    }

    isMutAttrCreated = true;

    if ( pthread_mutexattr_setprotocol( &mutex_attributes,
                                        PTHREAD_PRIO_INHERIT ) != 0 )
    {
        errno_print( "mutex attributes set protocol" );
        goto END;
    }
    if ( pthread_mutex_init( &mutexSynchronome, &mutex_attributes ) != 0 )
    {
        errno_print( "mutex init" );
        goto END;
    }

    
    isMutCreated = true;

    threadMaxPriority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( threadMaxPriority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }
    
    
    // Set thread attributes for the servicea that do the image loading and
    // image selection (HIGH PRIORITY).


    isPass = set_thread_attributes( &attrLoad,
                                    &paramsLoad,
                                    MY_SCHED_POLICY,
                                    threadMaxPriority - 1,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: load service\n");
        goto END;
    }

    isPass = set_thread_attributes( &attrSelect,
                                    &paramsSelect,
                                    MY_SCHED_POLICY,
                                    threadMaxPriority - 2,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: select service\n");
        goto END;
    }

    // Set thread attributes for the service that does the image processing and
    // image writing (SLACK STEALER).
    isPass = set_thread_attributes( &attrProcessWrite,
                                    &paramsProcessWrite,
                                    MY_SCHED_POLICY,
                                    threadMaxPriority - 3,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: process + write service\n");
        goto END;
    }


    // Create the service thread that handles image loading.
    rv =  pthread_create(&threadLoad,  // pointer to thread descriptor
                         &attrLoad,    // use specific attributes
                         service_load, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating load service\n" );
        goto END;
    }

    // Create the service thread that handles image selection.
    rv =  pthread_create(&threadSelect,  // pointer to thread descriptor
                         &attrSelect,    // use specific attributes
                         service_select, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating select service\n" );
        goto END;
    }

    // Create the service thread that handles image processing and writing.
    rv =  pthread_create(&threadProcessWrite,  // pointer to thread descriptor
                         &attrProcessWrite,    // use specific attributes
                         service_process_and_write, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating process + write service\n" );
        goto END;
    }

    // Set up a timer which will trigger the execution of the sequencer
    // whenever it counts to zero.
    /* set up to signal SIGALRM if timer expires */
    remainingSequencePeriods = sequencePeriods;
    timer_create( CLOCK_REALTIME, NULL, &sequencerTimer );
    signal(SIGALRM, (void(*)()) sequencer);

    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = UNIT_TIME_MS * MILLI_TO_NANO; // refill timer with this
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = UNIT_TIME_MS * MILLI_TO_NANO; // initial value.

    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );


    // Wait for the load and select services thread to complete
    rv = pthread_join( threadLoad, NULL );
    if ( rv < 0)
    {
        errno_print( "error joining with load + select service\n");
    }
    
    // Wait for the process and write services thread to complete
    rv = pthread_join( threadProcessWrite, NULL );
    if ( rv < 0)
    {
        errno_print( "error joining with process + write service\n");
    }

    // Disable the timer
    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = 0;
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = 0;
    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );

END:

    if ( isOkLoadSem == true ) sem_destroy( &semLoad );
    if ( isOkSelectSem == true ) sem_destroy( &semSelect );
    if ( isMutAttrCreated == true )
    {
        pthread_mutexattr_destroy( &mutex_attributes );
    }
    if ( isMutCreated == true ) pthread_mutex_destroy( &mutexSynchronome );
    
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
    static unsigned int curPeriod = 0;
    //printf( "in the SE-SE-SEQUENCER!!\n" );

    pthread_mutex_lock( &mutexsynchronome );
    if ( remainingSequencePeriods > 0 )
    {
        remainingSequencePeriods--;
    }
    
    pthread_mutex_unlock( &mutexSynchronome );
    if ( ( curPeriod % T_LOAD_SELECT ) == 0 )
    {
        sem_post( &semLoad );
        sem_post( &semSelect );
    }

    curPeriod ++;
}


/******************************************************************************
 *
 * service_load
 *
 *
 * Description: runs the image loading service. Will queue up buffers for v4l2
 *              to store image data in and send buffers to the selection
 *              service.
 *
 * Arguments:   threadp (IN): Contains the parameters to be used by the tests.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *service_load( void *threadp )
{
    bool isServiceContinue = true;
    bool isMqCreated = false;
    bool isFail = false;
    struct timespec startTime;
    struct timespec endTime;
    double deltaTimeUs;
    int curRun = 0;
    struct v4l2_state *state = ( (struct thread_args *)threadp )->state;
    mqd_t mq_to_selection;
    bool rv;
    int curBuf;
    int prevBuf;
    int prevPrevBuf;
    struct load_to_select_msg msgOut;
    int mmapIdx1;
    int mmapIdx2;
    

    rv = init_message_queue( &mq_to_selection, TO_SELECTION_MQ );
    if ( rv == false )
    {
        isFail = true;
        goto END;
    }
    isMqCreated = true;


    // Load the first image. Will need 2 to start selecting valid images by
    // comparing them.
    if ( queue_stream_bufs( 1, state ) == false )
    {
       isFail = true;
       goto END;
    }
    
    sem_wait( &semLoad );

    // Read the buffer we started downloading last period.
    if ( read_frame_stream( &mmapIdx1, state ) == false )
    {
        isFail = true;
        goto END;
    }

    // Load the second image. Will need 2 to start selecting valid images by
    // comparing them.
    if ( queue_stream_bufs( 2, state ) == false )
    {
       isFail = true;
       goto END;
    }

    curBuf = 2;
    prevBuf = 1;
    prevPrevBuf = 0;

    while ( isServiceContinue )
    {
        if ( sem_wait( &semLoad ) != 0 )
        {
            errno_print( "sem_wait load\n");
            isFail = true;
            goto END;
        }
        curRun++;
        
        curBuf = ( curBuf + 1 ) % NUM_CAMERA_BUFFERS;
        prevBuf = ( prevBuf + 1 ) % NUM_CAMERA_BUFFERS;
        prevPrevBuf = ( prevPrevBuf + 1 ) % NUM_CAMERA_BUFFERS;

        if ( queue_stream_bufs( curBuf, state ) == false )
        {
           isFail = true;
           goto END;
        }

        // Read the buffer we started downloading last period.
        mmapIdx2 = mmapIdx1;
        if ( read_frame_stream( &mmapIdx1, state ) == false )
        {
            isFail = true;
            goto END;
        }

        // sanity check. Ensure the last two frames read have the same length.
        if ( state->bufferList[mmapIdx1].length !=
             state->bufferList[mmapIdx2].length )
        {
            isFail = true;
            goto END;
        }

        // Send message out to be picked up by the selection service.
        msgOut.buf1 = state->bufferList[mmapIdx1].start;
        msgOut.buf2 = state->bufferList[mmapIdx2].start;
        msgOut.bufLen = state->bufferList[mmapIdx1].length;
        if ( mq_send( mq_to_selection,
                      (char *)&msgOut,
                      sizeof(msgOut), 30) != 0 )
        {
            errno_print( "mq_send load->select\n");
            isFail = true;
            goto END;
        }

        pthread_mutex_lock( &mutexSynchrome );
    	if ( remainingSequencePeriods == 0 )
    	{
    	    isServiceContinue = false;
    	}
    	pthread_mutex_unlock( &mutexSynchronome );

    }
        
END:
    if ( isMqCreated == true )
    {
        mq_close( mq_to_selection );
    }

    if ( isFail == true )
    {
        pthread_mutex_lock( &mutexSynchrome );
        remainingSequencePeriods = 0; // end all services
        pthread_mutex_unlock( &mutexSynchrome );
    }

    return (void *)NULL;
}


/******************************************************************************
 *
 * service_select
 *
 *
 * Description: runs the image selection service. Recieves 2 buffers from the
 *              image loading service and compares them. If an image meets the
 *              selection criteria, it is sent to the process+write service.
 *
 * Arguments:   threadp (IN): Contains the parameters to be used by the tests.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *service_select( void *threadp )
{
    bool isServiceContinue = true;
    bool isMqFromLoadCreated = false;
    bool isMqToProcessing = false;
    bool isFail = false;
    //struct v4l2_state *state = ( (struct thread_args *)threadp )->state;
    mqd_t mq_from_load;
    mqd_t mq_to_processing;
    bool rv;
    struct load_to_select_msg msgIn;
    struct select_to_process_msg msgOut;
    char *bufNewer;
    char *bufOlder;
    unsigned int bufLen;
    int count = 0;
    

    rv = init_message_queue( &mq_from_load, TO_SELECTION_MQ );
    if ( rv == false )
    {
        isFail = true;
        goto END;
    }
    isMqFromLoadCreated = true;

    rv = init_message_queue( &mq_to_processing, TO_PROCESSING_MQ );
    if ( rv == false )
    {
        isFail = true;
        goto END;
    }
    isMqToProcessingCreated = true;


    while ( isServiceContinue )
    {
        if ( sem_wait( &semSelect ) != 0 )
        {
            errno_print( "sem_wait select\n");
            isFail = true;
            goto END;
        }

        if ( mq_receive( mq_from_load,
                         (char *)&msgIn,
                         sizeof(msgIn),
                         NULL ) == -1 )
        {
            if ( errno != EAGAIN )
            {    
            	errno_print( "mq_recieve select\n");
            	isFail = true;
            	goto END;
            }
        }
        else
        {
            printf( "in selection task: got msg from load\n" );
        }

        bufNewer = msgIn.buf1;
        bufOlder = msgIn.buf2;
        bufLen = msgIn.bufLen;

        //selectImage( bufNewer, bufOlder, bufLen );


        if ( count % 4 == 0 )
        {
            if ( mq_send( mq_to_selection,
                          (char *)&msgOut,
                          sizeof(msgOut), 30) != 0 )
            {
                errno_print( "mq_send select->process\n");
                isFail = true;
                goto END;
            }
        }
        count++;


        pthread_mutex_lock( &mutexSynchrome );
    	if ( remainingSequencePeriods == 0 )
    	{
    	    isServiceContinue = false;
    	}
    	pthread_mutex_unlock( &mutexSynchronome );
    }
        
END:
    if ( isMqFromLoadCreated == true )
    {
        mq_close( mq_from_load );
    }
    
    if ( isMqToProcessingCreated == true )
    {
        mq_close( mq_to_processing );
    }

    if ( isFail == true )
    {
        pthread_mutex_lock( &mutexSynchrome );
        remainingSequencePeriods = 0; // end all services
        pthread_mutex_unlock( &mutexSynchrome );
    }

    return (void *)NULL;
}

/******************************************************************************
 *
 * service_process_and_write
 *
 *
 * Description: runs the image processing service. Whenever an image is in the
 *              message queue, this service does any processing on it and then
 *              writes it to a file.
 *
 * Arguments:   threadp (IN): Contains the parameters to be used by the tests.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *service_process_and_write( void *threadp )
{
    bool isServiceContinue = true;
    bool isMCreated = false;
    bool isFail = false;
    //struct v4l2_state *state = ( (struct thread_args *)threadp )->state;
    mqd_t mq_from_load;
    mqd_t mq_to_processing;
    bool rv;
    struct select_to_process_msg msgIn;
    char *buf;
    unsigned int bufLen;
    

    rv = init_message_queue( &mq_from_selection, TO_PROCESSING_MQ );
    if ( rv == false )
    {
        isFail = true;
        goto END;
    }
    isMqCreated = true;



    while ( isServiceContinue )
    {
        if ( mq_receive( mq_from_load,
                         (char *)&msgIn,
                         sizeof(msgIn),
                         NULL ) == -1 )
        {
            if ( errno != EAGAIN )
            {    
            	errno_print( "mq_recieve process+write\n");
            	isFail = true;
            	goto END;
            }
        }
        else
        {
            printf( "process + write task recieved msg\n" );
        }

        buf = msgIn.buf;
        bufLen = msgIn.bufLen;




        pthread_mutex_lock( &mutexSynchrome );
    	if ( remainingSequencePeriods == 0 )
    	{
    	    isServiceContinue = false;
    	}
    	pthread_mutex_unlock( &mutexSynchronome );
    }
        
END:
    if ( isMqCreated == true )
    {
        mq_close( mq_from_selection );
    }

    if ( isFail == true )
    {
        pthread_mutex_lock( &mutexSynchrome );
        remainingSequencePeriods = 0; // end all services
        pthread_mutex_unlock( &mutexSynchrome );
    }

    return (void *)NULL;
}


///******************************************************************************
// *
// * loadImage
// *
// *
// * Description: Test to see how long it takes for the camera to produce an
// *              image. First QUEUES a buffer so the camera knows a buffer is
// *              ready to write in. Then it DEQUEUES the buffer to have the
// *              application wait for the image to be loaded by the camera.
// *              This test assumes mmap (memory mapped) buffer communication
// *              between the application and the camera.
// *
// * Arguments:   state (IN/OUT): v4l2 state with the information of an open
// *              video device.
// *
// * Return:      true if successful, false otherwise.
// *
// *****************************************************************************/
//static bool loadImage( struct v4l2_state *state )
//{
//    bool isPass = true;
//    int readyBufIndex;
//
//
//    // Tell the camera that a buffer ready to write in
//    isPass = queue_stream_bufs( state->curBufIndex,
//                                state );
//
//    // Wait for the camera to write into the buffer 
//    isPass = read_frame_stream( &readyBufIndex, state );
//
//    if ( state->curBufIndex != (unsigned)readyBufIndex )
//    {
//        printf( "load image error: current buffer = %u, dequeued buffer %i",
//                state->curBufIndex,
//                readyBufIndex );
//    }
//
//    // go to the next buffer. Since we only use 2 buffers, the next will be
//    // either buffer 0 or buffer 1.
//    state->curBufIndex = ( state->curBufIndex + 1 ) & 0x1;
//
//    return isPass;
//}


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
    bool isPass = true;
    int diff;
    int passLimit = 100;

    // Ensure lengths of the images are the same
    if ( state->bufferList[0].length != state->bufferList[1].length )
    {
        printf( "image length discrepancy buf0 = %lu, buf1 = %lu\n",
                state->bufferList[0].length,
                state->bufferList[1].length );
        return false;
    }

    // Check to see if the images are equivalent
    diff = memcmp( state->bufferList[0].start ,
                   state->bufferList[1].start,
                   state->bufferList[0].length );
    
    if ( diff < 0 )
    {
        diff *= -1;
    }
    if ( diff > passLimit )
    {
        isPass = false;
    }

    return isPass;
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

    addCoolBorder( state->bufferList[latestImageIndex].start,
                   state->bufferList[latestImageIndex].length,
                   state->formatData.fmt.pix.width );

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

    //char ppm_header[]="P6\n#uname\n#9999999999 sec 9999999999 usec \n"HRES_STR" "VRES_STR"\n255\n"
    calculatedStampSize = 3 + // P6\n
                          unameBufLen + 2 + // #<uname -a result>\n
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
             "P6\n#%s\n#%010u sec %010u usec \n%u %u\n255\n",
             unameBuf,
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
 * addCoolBorder
 *
 *
 * Description: Adds a border to the frame that is very blue.
 *
 * Arguments:   yuyvBufIn (IN): buffer holding an image in YUYV format.
 *
 *              yuyvBufLen (IN): Size, in bytes, of the YUYV image.
 *
 *              pixelsPerLine (IN):  How many pixels are in each line of the
 *                                   image.
 *
 * Return:      N/A
 *
 *****************************************************************************/
static void addCoolBorder( char *yuyvBufIn, int yuyvBufLen, int pixelsPerLine )
{
    int i;
    int j;
    int yuyvBytesPerLine = 2 * pixelsPerLine;
    unsigned char *pptr = (unsigned char *)yuyvBufIn;

    // top border
    for ( i = 0; i < yuyvBufLen / 16; i += 4 )
    {
        pptr[i + 1] |= 0xff; // Max out U (Cb)
    }

    for ( i = yuyvBufLen / 16;
          i < yuyvBufLen - yuyvBufLen / 16;
          i += yuyvBytesPerLine )
    {
        // left border
        //for( j = 0; j < yuyvBytesPerLine / 16; j += 4 )
        //{
        //    pptr[i + j + 1] |= 0xff; // Max out U (Cb)
        //}

        // Left and right borders
        // not sure why the leftmost and rightmost border starts are bounded as
        // follows
        // leftmost: from ( yuyvBytesPerLine / 16 ) * 4
        //           to ( yuyvBytesPerLine / 16 ) * 5
        // rightmost: from ( yuyvBytesPerLine / 16 ) * 3
        //            to ( yuyvBytesPerLine / 16 ) * 4
        for( j = ( yuyvBytesPerLine / 16 ) * 3;
             j < ( yuyvBytesPerLine / 16 ) * 5;
             j += 4 )
        {
            pptr[i + j + 1] |= 0xff; // Max out U (Cb)
        }

        // right border
        //for( j = yuyvBytesPerLine - yuyvBytesPerLine / 16;
        //     j < yuyvBytesPerLine;
        //     j += 4 )
        //{
        //    pptr[i + j + 1] |= 0xff; // Max out U (Cb)
        //}
    }

    // bottom border
    for ( i = yuyvBufLen - yuyvBufLen / 16; i < yuyvBufLen; i += 4 )
    {
        pptr[i + 1] |= 0xff; // Max out U (Cb)
    }
    
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
 * init_message_queue
 *
 *
 * Description: Creates a message queue
 *
 * Arguments:   mqDescriptor (OUT): descriptor for the new message queue.
 *
 *              mqName (OUT): name of the new message queue.
 *
 * Return:      true if the message queue was created successfully. Otherwise
 *              returns false.
 *
 *****************************************************************************/
static bool init_message_queue( mqd_t *mqDescriptor, char *mqName )
{
    bool isPass = true;
    bool isMqCreated = false;
    int is_init_failure = 0;
    struct mq_attr mqAttributes;

    
    /* setup common message q attributes */
    mqAttributes.mq_maxmsg = NUM_BUFFERS;
    mqAttributes.mq_msgsize = sizeof( struct load_to_select_msg );
    mqAttributes.mq_flags = O_NONBLOCK;
    mqAttributes.mq_curmsgs = 0;
    

    // create message queue for loading service to message image processing
    // serivce
    *mqDescriptor = mq_open( mqName,
                             O_CREAT|O_RDWR,
                             S_IRWXU,
                             &mqAttributes );
    if ( *mqDescriptor == (mqd_t)(-1) )
    {
        errno_print( "init_message_queue\n" );
        isPass = false;
        goto END;
    }

    isMqCreated = true;

   // needed since mq_open() ignores the mq_flags attribute
    if ( mq_setattr( *mqDescriptor,
                     &mqAttributes,
                     NULL ) == -1 )
    {
        errno_print( "init_message_queue\n" );
        isPass = false;
    }

END:

    if ( ( isPass == false ) && ( isMqCreated == true ) )
    {
        mq_close( *mqDescriptor );
    }
    
    return isPass;
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

#endif // #ifndef _SYNCHRONOME_SRV_C
