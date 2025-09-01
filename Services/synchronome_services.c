#include "synchronome_services.h"

#ifndef _SYNCHRONOME_SRV_C
#define _SYNCHRONOME_SRV_C

#define CORE_0 0
#define CORE_1 1
#define CORE_2 2
#define CORE_3 3

#define TO_PROCESSING_MQ "/to_processing_mq"
#define TO_SELECTION_MQ "/to_selection_mq"

#define SEC_TO_NANO 1000000000
#define MILLI_TO_NANO 1000000
#define MICRO_TO_NANO 1000


#if OPERATE_AT_10HZ

#define UNIT_TIME_MS  33        // period (millisecs) for the sequencer to run
#define T_LOAD_SELECT 1         // # of sequencer periods for load and select services
#define DIFF_THRESHOLD 0.15     // Threshold for the select service to distinguish unique frames
#define STABLE_FRAMES_THRESHOLD 1 // # of stable frames required for a new image to be selected

#else // #if OPERATE_AT_10HZ

#define UNIT_TIME_MS 33
#define T_LOAD_SELECT 4
#define DIFF_THRESHOLD 0.005
#define STABLE_FRAMES_THRESHOLD 3

#endif // #if OPERATE_AT_10HZ


#if RECORD_SERVICES_LOG
#define SERVICE_LOG(str, arg1, arg2, arg3) \
    syslog( LOG_CRIT, (str), (arg1), (arg2), (arg3) )
#else // #if RECORD_SERVICES_LOG
// to remove "set but not used" warning for arg3
#define SERVICE_LOG(str, arg1, arg2, arg3) if(arg3 > 0.0){ }
#endif // #if RECORD_SERVICES_LOG #else

#if RECORD_IMG_CAPTURE_LOG
#define IMG_CAPTURE_LOG(str, arg1, arg2) syslog( LOG_CRIT, (str), (arg1), (arg2) )
#else // #if RECORD_IMG_CAPTURE_LOG
// to remove "set but not used" warning for arg2
#define IMG_CAPTURE_LOG(str, arg1, arg2) if(arg2 > 0.0){ }
#endif // #if RECORD_IMG_CAPTURE_LOG #else

#if DUMP_DIFFS
#define DIFF_LOG(str, arg1, arg2, arg3) \
    syslog( LOG_CRIT, (str), (arg1), (arg2), (arg3) )
#else // #if DUMP_DIFFS
// to remove "set but not used" warning for arg2
#define DIFF_LOG(str, arg1, arg2, arg3)
#endif // #if DUMP_DIFFS #else

#if RECORD_QUEUE_SIZE
#define QUEUE_LOG(str, arg1, arg2, arg3) \
    syslog( LOG_CRIT, (str), (arg1), (arg2), (arg3) )
#else // #if RECORD_QUEUE_SIZE
// to remove "set but not used" warning for arg2
#define QUEUE_LOG(str, arg1, arg2, arg3)
#endif // #if RECORD_QUEUE_SIZE #else

struct thread_args
{
    struct v4l2_state *state;
};

struct load_to_select_msg
{
    char *buf1;
    char *buf2;
    unsigned int msgLen;
    unsigned int heightInPixels;
    unsigned int widthInPixels;
    struct timespec imageCaptureTime;
};

struct select_to_process_msg
{
    char *buf;
    unsigned int msgLen;
    unsigned int heightInPixels;
    unsigned int widthInPixels;
    struct timespec imageCaptureTime;
    unsigned int imageCount;
};

union general_msg
{
    struct load_to_select_msg loadToSelectMsg;
    struct select_to_process_msg selectToProcessMsg;
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

unsigned int remainingPics;

bool isLoadContinue = true;
bool isSelectContinue = true;
bool isProcessWriteContinue = true;
unsigned int programEnding = 0;

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
char *selectOutBuf = NULL;
unsigned int frameBufLen;


char *servicesLogString = "[Course #:4] [Final Project] [Run Count: %u] "
                          "[%s Elapsed Time: %lf us]";
char *recordImageLogString = "[Course #:4] [Final Project] [Frame Count: %u] "
                             "[Image Capture Start Time: %lf seconds]";
char *queueLogString = "[Final Project] %s mqsize (%u): %ld";


static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int schedulingPolicy,
                                   int priority,
                                   int affinity );

static void *starter( void *threadp );

void sequencer( int id );

static void *service_load( void *threadp );

static void *service_select( void *threadp );

static void *service_process_and_write( void *threadp );

static double calc_array_diff_8bit( char *bufNewer,
                                    char *bufOlder,
                                    unsigned int msgLen );

static bool processImage( char *frameData,
                          unsigned int frameLen,
                          unsigned int frameWidth,
                          char *outBuffer,
                          unsigned int *outFrameSize );

static void dump_ppm( const void *p,
                      int size,
                      char *header,
                      int headerSize,
                      unsigned int tag );

static bool add_ppm_header( char *buf,
                            unsigned int bufLen,
                            unsigned int seconds,
                            unsigned int microSeconds,
                            unsigned int horizontalResolution,
                            unsigned int verticalResolution,
                            unsigned int *stampLen );

#if !OUTPUT_YUYV_PPM
static int convert_yuyv_image_to_rgb( char *yuyvBufIn,
                                       int yuyvBufLen,
                                       char *rgbBufOut );
static void yuv2rgb(int y,
                    int u,
                    int v,
                    unsigned char *r,
                    unsigned char *g,
                    unsigned char *b);
#endif // #if !OUTPUT_YUYV_PPM

#if USE_COOL_BORDER
static void addCoolBorder( char *yuyvBufIn, int yuyvBufLen, int pixelsPerLine );
#endif // #if USE_COOL_BORDER

static bool init_message_queue( mqd_t *mqDescriptor, char *mqName, bool isBlocking );

static void print_scheduler(void);

/******************************************************************************
 *
 * run_synchronome
 *
 *
 * Description: Starts the starter thread (real-time thread) and sets up
 *              resources used by the synchronome services.
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


    // Uname -a output for syslog and ppm header
    FILE* unameOutput = popen("uname -a", "r");
    if ( fgets( unameBuf, sizeof(unameBuf), unameOutput) == NULL )
    {
        errno_print( "ERROR: uname -a output" );
        goto END;
    }
    pclose( unameOutput );
    unameBufLen = (unsigned int)strlen( unameBuf );

    syslog( LOG_CRIT, "[Course #:4] [Final Project] %s", unameBuf );
    

    // Delete the message queues if they already exists
    if ( mq_unlink( TO_SELECTION_MQ ) != 0 )
    {
        errno_print( "ERROR: TO_SELECTION_MQ unlink 1" );
    }
    if ( mq_unlink( TO_PROCESSING_MQ ) != 0 )
    {
        errno_print( "ERROR: TO_PROCESSING_MQ unlink 1" );
    }


    // Dynamically allocate memory to create working buffers for frame
    // processing. It is tied to the message queue size so that we can fill
    // up the message queue and not have to overwrite any data.
    frameBufLen = state->processedImageSize;
    selectOutBuf = malloc( frameBufLen * NUM_MSG_QUEUE_BUFS );
    if ( selectOutBuf == NULL )
    {
        errno_print( "ERROR: run_synchronome malloc" );
        goto END;
    }

    

    // Set thread attributes of the image loading service, the image processing
    // service and the scheduler.
    threadMaxPriority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( threadMaxPriority == -1 )
    {
        errno_print("ERROR: error getting minimum priority");
        goto END;
    }

    // set starter service thread attributes
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

    printf( "run_synchronome\n" );

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


    if ( selectOutBuf != NULL )
    {
        free( selectOutBuf );
        selectOutBuf = NULL;
    }
    
    // clean up message queue when finished
    if ( mq_unlink( TO_SELECTION_MQ ) != 0 )
    {
        errno_print( "ERROR: TO_SELECTION_MQ unlink 2" );
    }
    if ( mq_unlink( TO_PROCESSING_MQ ) != 0 )
    {
        errno_print( "ERROR: TO_PROCESSING_MQ unlink 2" );
    }

    printf( "end run synchronome\n" );

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
        errno_print( "ERROR: pthread_attr_init" );
        goto END;
    }

    rc = pthread_attr_setinheritsched( attr, PTHREAD_EXPLICIT_SCHED);
    if ( rc != 0 )
    {
        errno_print( "ERROR: pthread_attr_setinheritsched" );
        goto END;
    }

    rc = pthread_attr_setschedpolicy( attr, schedulingPolicy );
    if ( rc != 0 )
    {
        errno_print( "ERROR: pthread_attr_setschedpolicy" );
        goto END;
    }

    // set priority
    params->sched_priority = priority;
    rc = pthread_attr_setschedparam( attr, params );
    if ( rc != 0 )
    {
        errno_print( "ERROR: pthread_attr_setschedparam" );
        goto END;
    }

    // set processor affinity
    CPU_ZERO(&thread_cpu);
    cpu_idx = affinity ;
    CPU_SET(cpu_idx, &thread_cpu);
    rc = pthread_attr_setaffinity_np( attr, sizeof(thread_cpu), &thread_cpu );
    if ( rc != 0 )
    {
        errno_print( "ERROR: pthread_attr_setaffinity_np" );
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
    int threadMinPriority;
    timer_t sequencerTimer;
    struct itimerspec intervalTime = { {1,0}, { 1,0 } };
    struct itimerspec oldIntervalTime = { {1,0}, { 1,0 } };
    int flags = 0;
    bool isOkLoadSem = false;
    bool isOkSelectSem = false;
    bool isStartedLoadService = false;
    bool isStartedSelectService = false;
    bool isStartedProcessWriteService = false;
    bool isTimerCreated = false;
    bool isMutAttrCreated = false;
    bool isMutCreated = false;
    pthread_mutexattr_t mutex_attributes;

    printf( "starter " );
    print_scheduler( );

    // Initialize semaphores
    if ( sem_init( &semLoad, 0, 0 ) != 0 )
    {
        errno_print( "ERROR: semaphore init" );
        goto END;
    }
    isOkLoadSem = true;

    if ( sem_init( &semSelect, 0, 0 ) != 0 )
    {
        errno_print( "ERROR: semaphore init" );
        goto END;
    }
    isOkSelectSem = true;



    // Initialize mutex with priority inheritance
    if ( pthread_mutexattr_init( &mutex_attributes ) != 0 )
    {
        errno_print( "ERROR: mutex attributes init" );
        goto END;
    }
    
    isMutAttrCreated = true;
    
    if ( pthread_mutexattr_setprotocol( &mutex_attributes,
                                        PTHREAD_PRIO_INHERIT ) != 0 )
    {
        errno_print( "ERROR: mutex attributes set protocol" );
        goto END;
    }
    if ( pthread_mutex_init( &mutexSynchronome, &mutex_attributes ) != 0 )
    {
        errno_print( "ERROR: mutex init" );
        goto END;
    }

    isMutCreated = true;


    // get max and min thread priorities
    threadMaxPriority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( threadMaxPriority == -1 )
    {
        errno_print("error getting maximum priority");
        goto END;
    }
    printf( "Max scheduling priority is %i\n", threadMaxPriority );
    
    threadMinPriority = sched_get_priority_min( MY_SCHED_POLICY );
    if ( threadMinPriority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }
    printf( "Min scheduling priority is %i\n", threadMinPriority );
    
    
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
    // image writing (SLACK STEALER). It will run on it's own processor.
    isPass = set_thread_attributes( &attrProcessWrite,
                                    &paramsProcessWrite,
                                    MY_SCHED_POLICY,
                                    threadMinPriority,
                                    CORE_2 );
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
    if ( rv != 0 )
    {
        errno_print( "ERROR: error creating load service\n" );
        goto END;
    }
    isStartedLoadService = true; 
    
    // Create the service thread that handles image selection.
    rv =  pthread_create(&threadSelect,  // pointer to thread descriptor
                         &attrSelect,    // use specific attributes
                         service_select, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv != 0 )
    {
        errno_print( "ERROR: error creating select service\n" );
        goto END;
    }
    isStartedSelectService = true;

    // Create the service thread that handles image processing and writing to
    // a file.
    rv =  pthread_create(&threadProcessWrite,  // pointer to thread descriptor
                         &attrProcessWrite,    // use specific attributes
                         service_process_and_write, // thread function entry point
                         threadp );    // parameters to pass in
    if ( rv != 0 )
    {
        errno_print( "ERROR: error creating process+write service\n" );
        goto END;
    }
    isStartedProcessWriteService = true;


    printf( "All service threads created\n" );

    // Set up a timer which will trigger the execution of the sequencer
    // whenever it counts to zero.
    /* set up to signal SIGALRM if timer expires */
    
    signal(SIGALRM, (void(*)()) sequencer);

    if ( timer_create( CLOCK_REALTIME, NULL, &sequencerTimer ) != 0 )
    {
        errno_print( "ERROR: timer_create\n" );
        pthread_mutex_lock( &mutexSynchronome );
        remainingPics = 0;
        pthread_mutex_unlock( &mutexSynchronome );
        goto END; 
    }
    isTimerCreated = true;
    pthread_mutex_lock( &mutexSynchronome );
    remainingPics = NUM_PICS;
    pthread_mutex_unlock( &mutexSynchronome );
    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = UNIT_TIME_MS * MILLI_TO_NANO; // refill timer with this
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = UNIT_TIME_MS * MILLI_TO_NANO; // initial value.

    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );


END:

    // Wait for the load service thread to complete
    if ( isStartedLoadService == true )
    {
    	rv = pthread_join( threadLoad, NULL );
    	if ( rv < 0)
    	{
    	    errno_print( "ERROR: error joining with load service\n");
    	}
    }
    
    // Wait for the select service thread to complete
    if ( isStartedSelectService == true )
    {
    	rv = pthread_join( threadSelect, NULL );
    	if ( rv < 0)
    	{
    	    errno_print( "ERROR: error joining with select service\n");
    	}
    }

    // Wait for the process and write services thread to complete
    if (  isStartedProcessWriteService == true )
    {
    	rv = pthread_join( threadProcessWrite, NULL );
    	if ( rv < 0)
    	{
    	    errno_print( "ERROR: error joining with process + write service\n");
    	}
    }


    if ( isTimerCreated == true )
    {
        // Disable the timer
        intervalTime.it_interval.tv_sec = 0;
    	intervalTime.it_interval.tv_nsec = 0;
    	intervalTime.it_value.tv_sec = 0;
    	intervalTime.it_value.tv_nsec = 0;
    	timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );
    }

    if ( isOkLoadSem == true ) sem_destroy( &semLoad );
    if ( isOkSelectSem == true ) sem_destroy( &semSelect );
    if ( isMutAttrCreated == true )
    {
        pthread_mutexattr_destroy( &mutex_attributes );
    }
    if ( isMutCreated == true ) pthread_mutex_destroy( &mutexSynchronome );

    printf( "end starter service\n" );
    
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
    unsigned int rp;


    pthread_mutex_lock( &mutexSynchronome );
    rp = remainingPics;
    pthread_mutex_unlock( &mutexSynchronome );
    
    
    if ( ( rp > 0 ) && ( programEnding == 0 ) )
    {
        curPeriod++;

    	if ( ( curPeriod % T_LOAD_SELECT ) == 0 )
    	{
    	    sem_post( &semLoad );
    	    sem_post( &semSelect );
    	}
    	
    }
    else
    {
        isLoadContinue = false;
        isSelectContinue = false;
        isProcessWriteContinue = false;
        sem_post( &semLoad );
        sem_post( &semSelect );
    }
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
    bool isMqCreated = false;
    struct timespec startTime;
    struct timespec endTime;
    double deltaTimeUs;
    int count = 0;
    struct v4l2_state *state = ( (struct thread_args *)threadp )->state;
    mqd_t mq_to_selection;
    bool rv;
    int curBuf;
    int prevBuf;
    int prevPrevBuf;
    union general_msg msgOut;
    int mmapIdx1;
    int mmapIdx2;
    struct timespec startCaptureTimes[NUM_CAMERA_BUFFERS];
    struct mq_attr mqAttr;
    
    clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );
    count++;

    rv = init_message_queue( &mq_to_selection, TO_SELECTION_MQ, false );
    if ( rv == false )
    {
        goto END;
    }
    isMqCreated = true;

    
    // Load the first image. Will need 2 to start selecting valid images by
    // comparing them.
    curBuf = 0;
    if ( queue_stream_bufs( curBuf, state ) == false )
    {
        goto END;
    }
    clock_gettime( CLOCK_MONOTONIC_RAW, &(startCaptureTimes[curBuf]) );
    clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
    deltaTimeUs = timespec_to_double_us( &endTime ) -
                  timespec_to_double_us( &startTime );

    SERVICE_LOG( servicesLogString, count, "LOAD", deltaTimeUs );



    sem_wait( &semLoad );

    // early exit
    if ( isLoadContinue == false )
    {
        goto END;
    }

    clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );
    count++;

    // Read the buffer we started downloading last period.
    if ( read_frame_stream( &mmapIdx1, state ) == false )
    {
        goto END;
    }

    // Load the second image. Once this loads the select service has enough
    // frames to start looking for stable images.
    if ( queue_stream_bufs( 1, state ) == false )
    {
        goto END;
    }

    // the three indicies below will help us keep track of image buffers
    // so that images may be continually downloaded by the camera and compared.
    curBuf = 1; // buffer the camera will start to download an image to
    prevBuf = 0; // buffer containing the latest image downloaded
                 // by the camera
    prevPrevBuf = NUM_CAMERA_BUFFERS - 1; // previous image downloaded by the
                                          // camera
    clock_gettime( CLOCK_MONOTONIC_RAW, &(startCaptureTimes[curBuf]) );

    while ( true )
    {
        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );

        SERVICE_LOG( servicesLogString, count, "LOAD", deltaTimeUs );
        
        if ( sem_wait( &semLoad ) != 0 )
        {
            errno_print( "ERROR: sem_wait load\n");
            break;
        }

        // For early exit or exit when we have gathered enough images
        if ( isLoadContinue == false )
        {
            break;
        }

        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );
        count++;
        
        curBuf = ( curBuf + 1 ) % NUM_CAMERA_BUFFERS;
        prevBuf = ( prevBuf + 1 ) % NUM_CAMERA_BUFFERS;
        prevPrevBuf = ( prevPrevBuf + 1 ) % NUM_CAMERA_BUFFERS;

        // let the camera use a buffer to download a new image to.
        if ( queue_stream_bufs( curBuf, state ) == false )
        {
           break;
        }
        clock_gettime( CLOCK_MONOTONIC_RAW, &(startCaptureTimes[curBuf]) );

        // Read the buffer we started downloading last period.
        mmapIdx2 = mmapIdx1;
        if ( read_frame_stream( &mmapIdx1, state ) == false )
        {
            break;
        }

        // sanity check. Ensure the last two downloaded frames read have the
        // same length.
        if ( state->bufferList[mmapIdx1].length !=
             state->bufferList[mmapIdx2].length )
        {
            printf( "service_load sanity check 1 failed: mmapIdx1 buf len= %u, "
                    "mmapIdx2 buf len= %u\n",
                    (unsigned int)state->bufferList[mmapIdx1].length,
                    (unsigned int)state->bufferList[mmapIdx2].length );
            break;
        }
        // sanity check. Ensure the actual index of last downloaded frame, i.e.
        // mmapIdx1, corresponds to our record keeping variable for the last
        // downloaded fram, i.e. prevBuf
        if ( prevBuf != mmapIdx1 )
        {
            printf( "service_load sanity check 2 failed: "
                    "prevBuf=%i mmapIdx1=%i\n", prevBuf, mmapIdx1 );
            break;
        }


        // Send message out to be picked up by the selection service.
        msgOut.loadToSelectMsg.buf1 = state->bufferList[mmapIdx1].start;
        msgOut.loadToSelectMsg.buf2 = state->bufferList[mmapIdx2].start;
        msgOut.loadToSelectMsg.msgLen = state->bufferList[mmapIdx1].length;
        
        msgOut.loadToSelectMsg.heightInPixels =
            (unsigned int)state->formatData.fmt.pix.height;
        
        msgOut.loadToSelectMsg.widthInPixels =
            (unsigned int)state->formatData.fmt.pix.width;

        msgOut.loadToSelectMsg.imageCaptureTime = startCaptureTimes[prevBuf];

        mq_getattr( mq_to_selection, &mqAttr );
        QUEUE_LOG(queueLogString, "LOAD", count, mqAttr.mq_curmsgs );
        
        if ( mq_send( mq_to_selection,
                      (char *)&msgOut,
                      sizeof(msgOut), 30) != 0 )
        {
            errno_print( "ERROR: mq_send load->select\n");
            break;
        }

    }

END:
        
    if ( isMqCreated == true )
    {
        mq_close( mq_to_selection );
    }

    programEnding++;

    printf( "end load service\n" );

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
    bool isMqFromLoadCreated = false;
    bool isMqToProcessingCreated = false;
    mqd_t mq_from_load;
    mqd_t mq_to_processing;
    bool rv;
    union general_msg msgIn;
    union general_msg msgOut;
    char *bufNewer;
    char *bufOlder;
    unsigned int msgLen;
    int count = 0;
    struct mq_attr mqAttr;
    double percentDiff;
    unsigned int numStableFrames = 0;
    struct timespec startTime;
    struct timespec endTime;
    double deltaTimeUs;
    unsigned int outCount = 0;
    char *bufferToProcessing;


    clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );
    count++;

    // initialize two message queues. One to recieve images to compare from the
    // image loading service, the other to send new stable images to the
    // process + write service.
    rv = init_message_queue( &mq_from_load, TO_SELECTION_MQ, false );
    if ( rv == false )
    {
        goto END;
    }
    isMqFromLoadCreated = true;

    rv = init_message_queue( &mq_to_processing, TO_PROCESSING_MQ, false );
    if ( rv == false )
    {
        goto END;
    }
    isMqToProcessingCreated = true;


    while ( true )
    {
        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );

        SERVICE_LOG( servicesLogString, count, "SELECT", deltaTimeUs );
        
        if ( sem_wait( &semSelect ) != 0 )
        {
            errno_print( "ERROR: sem_wait select\n");
            goto END;
        }

        // for early exit or when all requested images have been saved.
        if ( isSelectContinue == false )
        {
            break;
        }

        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );
        count++;

        if ( mq_receive( mq_from_load,
                         (char *)&msgIn,
                         sizeof(msgIn),
                         NULL ) == -1 )
        {
            if ( errno == EAGAIN )
            {
                continue;
            }

            errno_print( "ERROR: mq_recieve select\n");
            break;
        }

        bufNewer = msgIn.loadToSelectMsg.buf1;
        bufOlder = msgIn.loadToSelectMsg.buf2;
        msgLen   = msgIn.loadToSelectMsg.msgLen;


        // Compare the two images.
        percentDiff = calc_array_diff_8bit( bufNewer, bufOlder, msgLen );

        DIFF_LOG( "[Final Project] %% diff @t=%.1lfms (count %u): %lf",
                  timespec_to_double_us( &startTime ) / 1000.0,
                  count,
                  percentDiff );
        
        
        if ( percentDiff > DIFF_THRESHOLD )
        {
            // case where a large difference in frames is encountered.
            // Note the instability by setting isStable to false.
            numStableFrames = 0;
            continue;         
        }

        numStableFrames++;
        

        if ( numStableFrames < STABLE_FRAMES_THRESHOLD )
        {
            // case where we have recently encountered a large difference
            // in frames (unstable) and we are trying to see if the frames
            // have settled to a new stable one.
            continue;
        }
        else if ( numStableFrames > STABLE_FRAMES_THRESHOLD )
        {
            // case where have seen a sequence of virtually unchanging fames...
            // Do nothing with them.
            continue;
        }

        
        // Only go here when numStableFrames == STABLE_FRAMES_THRESHOLD

        // Here the frames have settled to a new one. We can remove the
        // instability flag and send the frame for processing.
        outCount++;

        // select the buffer to send for processing. slectOutBuf is tied to the
        // message queue max size. This way the entire message queue can be filled
        // with unique frame data (without overwritting anything).
        bufferToProcessing =
            &selectOutBuf[(outCount % NUM_MSG_QUEUE_BUFS) * frameBufLen];
        
        memcpy( bufferToProcessing, bufNewer, msgLen );
        msgOut.selectToProcessMsg.buf = bufferToProcessing;
        msgOut.selectToProcessMsg.msgLen = msgLen;
        
        msgOut.selectToProcessMsg.heightInPixels =
            msgIn.loadToSelectMsg.heightInPixels;
        
        msgOut.selectToProcessMsg.widthInPixels =
            msgIn.loadToSelectMsg.widthInPixels;

        msgOut.selectToProcessMsg.imageCaptureTime =
            msgIn.loadToSelectMsg.imageCaptureTime;

        msgOut.selectToProcessMsg.imageCount = outCount;

        mq_getattr( mq_to_processing, &mqAttr );
        QUEUE_LOG(queueLogString, "SELECT", outCount, mqAttr.mq_curmsgs );

        if ( mq_send( mq_to_processing,
                      (char *)&msgOut,
                      sizeof(msgOut),
                      30) != 0 )
        {
            errno_print( "ERROR: mq_send select->process\n");
            break;
        }
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

    programEnding++;

    printf( "end select service\n" );

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
    bool isMqCreated = false;
    mqd_t mq_from_selection;
    bool rv;
    union general_msg msgIn;
    char *frameBuf;
    unsigned int frameLen;
    unsigned int frameHeight;
    unsigned int frameWidth;
    unsigned int processedFrameLen;
    char headerBuf[512];
    struct timespec imageCaptureTime;
    unsigned int seconds;
    unsigned int microSeconds;
    unsigned int stampLen;
    struct timespec startTime;
    struct timespec endTime;
    double deltaTimeUs;
    double imageCaptureTimeSec;
    unsigned int count = 0;
    unsigned int outCount;
    char *processBuf;

    rv = init_message_queue( &mq_from_selection, TO_PROCESSING_MQ, false );
    if ( rv == false )
    {
        goto END;
    }
    isMqCreated = true;

    processBuf = malloc( frameBufLen );
    if ( processBuf == NULL )
    {
        errno_print( "ERROR: run_synchronome malloc" );
        goto END;
    }

    while ( true )
    {
        if ( isProcessWriteContinue == false )
        {
            break;
        }


        clock_gettime( CLOCK_MONOTONIC_RAW, &startTime );

        if ( mq_receive( mq_from_selection,
                         (char *)&msgIn,
                         sizeof(msgIn),
                         NULL )  == -1 )
        {
            if ( errno != EAGAIN )
            {    
            	errno_print( "ERROR: mq_recieve process+write\n");
            	break;
            }
            else
            {
                // When no frame is received from the selection service. Keep
                // looping until one comes in.
                continue;
            }
        }


        count++;

        // Get details of new frame sent over by the selection service
        frameBuf = msgIn.selectToProcessMsg.buf;
        frameLen = msgIn.selectToProcessMsg.msgLen;
        frameHeight = msgIn.selectToProcessMsg.heightInPixels;
        frameWidth = msgIn.selectToProcessMsg.widthInPixels;
        imageCaptureTime = msgIn.selectToProcessMsg.imageCaptureTime;
        outCount = msgIn.selectToProcessMsg.imageCount;


        // process the frame
        rv = processImage( frameBuf,
                           frameLen,
                           frameWidth,
                           processBuf,
                           &processedFrameLen );
        if ( rv != true )
        {
            errno_print( "ERROR: processImage process+write\n");
            break;
        }

        // The frame shall be stored in flash as a .ppm file. Create the ppm
        // header here.
        seconds = imageCaptureTime.tv_sec;
        if ( (unsigned int)imageCaptureTime.tv_nsec >= SEC_TO_NANO )
        {
            seconds++;
            microSeconds = ( (unsigned int)imageCaptureTime.tv_nsec -
                             SEC_TO_NANO ) / MICRO_TO_NANO;
        }
        else
        {
            microSeconds = (unsigned int)imageCaptureTime.tv_nsec /
                           MICRO_TO_NANO;
        }
        


        rv = add_ppm_header( headerBuf,
                             sizeof(headerBuf),
                             seconds,
                             microSeconds,
                             frameWidth,
                             frameHeight,
                             &stampLen );
        if ( rv != true )
        {
            errno_print( "ERROR: add_ppm_header process+write\n");
            break;
        }

        // Write the frame to flash as a .ppm file.
        dump_ppm( processBuf,
                  processedFrameLen,
                  headerBuf,
                  stampLen,
                  outCount );


        pthread_mutex_lock( &mutexSynchronome );
        remainingPics--;
        pthread_mutex_unlock( &mutexSynchronome );
        
        clock_gettime( CLOCK_MONOTONIC_RAW, &endTime );
        deltaTimeUs = timespec_to_double_us( &endTime ) -
                      timespec_to_double_us( &startTime );

        SERVICE_LOG( servicesLogString, outCount, "PROCESS", deltaTimeUs );

        imageCaptureTimeSec = timespec_to_double_sec( &imageCaptureTime );
        IMG_CAPTURE_LOG( recordImageLogString, count, imageCaptureTimeSec );
    }
        
END:

    if ( isMqCreated == true )
    {
        mq_close( mq_from_selection );
    }

    if ( processBuf != NULL )
    {
        free( processBuf );
        processBuf = NULL;
    }

    programEnding++;

    printf( "end process + write service\n" );


    return (void *)NULL;
}




/******************************************************************************
 *
 * calc_array_diff_8bit
 *
 *
 * Description: Frame comparison algorithm. Outputs a number to represent a
 *              sort of "percent difference" between two buffers. Calculated as
 *              a sum of all the "differences" between the bytes of the two
 *              buffers.
 *
 * Arguments:   bufNewer (IN): First of two buffers to be compared.
 *
 *              bufOlder (IN): Second of two buffers to be compared.
 *
 *              msgLen (IN): Number of bytes to compare.
 *
 * Return:      The "percent difference"
 *
 *****************************************************************************/
static double calc_array_diff_8bit( char *bufNewer,
                                    char *bufOlder,
                                    unsigned int msgLen )
{
    unsigned int difference;
    unsigned int absCorrection;
    unsigned int diffSum = 0;
    unsigned int i;
    unsigned int absDifference;
    unsigned int filter;
    

    for( i = 0; i < msgLen; i++ )
    {
        // Get the difference between bytes of image data.
        difference = (unsigned int)( bufNewer[i] - bufOlder[i] );
        // The correction is used to get the absolute value. It is zero
        // if difference is positive ( since the sign bit is zero ).
        // If the difference is negative, it evaluates to 2X the number (but
        // positive)
        absCorrection = ( difference >> ( sizeof(difference) * 8 - 1 ) ) *
                        ( 2 * ( ~difference + 1 ) );
        absDifference = difference + absCorrection;
        // The filter is used to exclude noise seen as small variations in
        // difference per byte. It will evaluate to 1 if the high
        // nibble is > 0. This was obtained through experimentation.
        filter = ( absDifference >> 7 ) |
                 ( absDifference >> 6 ) |
                 ( absDifference >> 5 ) |
                 ( absDifference >> 4 );
        diffSum += absDifference * filter;
    }

    // 256 = number of bits in each byte of image data.
    // 100 to make a percentage.
    // We return a calculated % difference.
    return  ( ( (double)diffSum * 100.0 ) /
              ( (double)msgLen * 256.0 ) );

    // TODO: return only diffsum and adjust thresholds.
    
}



/******************************************************************************
 *
 * processImage
 *
 *
 * Description: Carries out image processing on the input frame. The type of
 *              processing depends on the compilation flags enabled.
 *
 *              - USE_COOL_BORDER: If enabled, adds a cool blue border to the
 *                                 image. If disabled, the image will not have
 *                                 this border.
 *
 *              - OUTPUT_YUYV_PPM: If enabled, the image will remain in YUYV
 *                                 format when saved as .ppm file. The image
 *                                 will look distorted in a weirdly asthetic
 *                                 way. If disabled, a normal RGB image will
 *                                 be saved and look normal under an image
 *                                 viewer.
 *
 * Arguments:   frameData (IN/OUT): Buffer containing the frame to process.
 *
 *              frameLen (IN): Length, in bytes, of the frame to process.
 *
 *              frameWidth (IN): The number of pixels of the incoming frame's
 *                               width. E.g. the 640 in 640x480.
 *
 *              outBuffer(OUT): Buffer to hold the processed frame.
 *
 *              outFrameSize(OUT): Size, in bytes, of the processed fame.
 *
 * Return:      true if successful, false otherwise.
 *
 * Remarks:     If USE_COOL_BORDER is enabled, the input frame is modified.
 *
 *****************************************************************************/
static bool processImage( char *frameData,
                          unsigned int frameLen,
                          unsigned int frameWidth,
                          char *outBuffer,
                          unsigned int *outFrameSize )
{
#if USE_COOL_BORDER
    addCoolBorder( frameData,
                   frameLen,
                   frameWidth );
#endif // #if USE_COOL_BORDER

#if !OUTPUT_YUYV_PPM
    // Transfer the image out of kernel space in RGB format
    *outFrameSize = convert_yuyv_image_to_rgb( frameData,
                                               frameLen,
                                               outBuffer );
#else
    memcpy( outBuffer, frameData, frameLen );
    *outFrameSize = frameLen;
#endif // #if !OUTPUT_YUYV_PPM #else

    return true;
}



/******************************************************************************
 *
 * make_ppm_header
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

    // Get the number of bytes of a string that displays the horizontal and
    // vertical resolution.
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


#if !OUTPUT_YUYV_PPM
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
#endif // #if !OUTPUT_YUYV_PPM

#if USE_COOL_BORDER
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
        // Left and right borders
        // not sure why the leftmost and rightmost border starts are bounded as
        // follows in a YUYV image
        // leftmost: from byte ( yuyvBytesPerLine / 16 ) * 4
        //           to byte ( yuyvBytesPerLine / 16 ) * 5
        // rightmost: from byte ( yuyvBytesPerLine / 16 ) * 3
        //            to byte ( yuyvBytesPerLine / 16 ) * 4
        for( j = ( yuyvBytesPerLine / 16 ) * 3;
             j < ( yuyvBytesPerLine / 16 ) * 5;
             j += 4 )
        {
            pptr[i + j + 1] |= 0xff; // Max out U (Cb)
        }
    }

    // bottom border
    for ( i = yuyvBufLen - yuyvBufLen / 16; i < yuyvBufLen; i += 4 )
    {
        pptr[i + 1] |= 0xff; // Max out U (Cb)
    }
    
}
#endif // #if USE_COOL_BORDER

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
 *              header (IN): Pointer to ppm header
 *
 *              headerSize (In): length of ppm header in bytes.
 *
 *              tag (IN):  A number which will go into the filename of the
 *                         created file.
 *
 * Return:      N/A
 *
 *****************************************************************************/
static void dump_ppm( const void *p,
                      int size,
                      char *header,
                      int headerSize,
                      unsigned int tag )
{
    int written, total, dumpfd;
    char ppm_dumpname[]="test00000000.ppm";
   
    snprintf(&ppm_dumpname[4], 9, "%08d", tag);
    memcpy( &ppm_dumpname[12], ".ppm", 4 );
    dumpfd = open(ppm_dumpname, O_WRONLY | O_NONBLOCK | O_CREAT, 00666);

    total = 0;
    do
    {
        written=write(dumpfd, header, headerSize);
        total+=written;
    } while(total < headerSize);

    total = 0;
    do
    {
        written=write(dumpfd, p, size);
        total+=written;
    } while(total < size);

    close(dumpfd);
    
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
 *              isBlocking (IN): specify if the queue should block if full
 *                               (mq_send) or empty (mq_recieve)
 *
 * Return:      true if the message queue was created successfully. Otherwise
 *              returns false.
 *
 *****************************************************************************/
static bool init_message_queue( mqd_t *mqDescriptor, char *mqName, bool isBlocking )
{
    bool isPass = true;
    bool isMqCreated = false;
    struct mq_attr mqAttributes;

    
    /* setup common message q attributes */
    mqAttributes.mq_maxmsg = NUM_MSG_QUEUE_BUFS;
    mqAttributes.mq_msgsize = sizeof(union general_msg);
    mqAttributes.mq_curmsgs = 0;

    if ( isBlocking == true )
    {
        mqAttributes.mq_flags = 0;
    }
    else
    {
        mqAttributes.mq_flags = O_NONBLOCK;
    }
    

    // create message queue for loading service to message image processing
    // serivce
    *mqDescriptor = mq_open( mqName,
                             O_CREAT|O_RDWR,
                             S_IRWXU,
                             &mqAttributes );
    if ( *mqDescriptor == (mqd_t)(-1) )
    {
        errno_print( "ERROR: init_message_queue\n" );
        isPass = false;
        goto END;
    }

    isMqCreated = true;

   // needed since mq_open() ignores the mq_flags attribute
    if ( mq_setattr( *mqDescriptor,
                     &mqAttributes,
                     NULL ) == -1 )
    {
        errno_print( "ERROR: init_message_queue\n" );
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
