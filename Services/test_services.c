#include "test_services.h"

#ifndef _TEST_SRV_C
#define _TEST_SRV_C

#define CORE_0 0
#define CORE_1 1
#define CORE_2 2
#define CORE_3 3

pthread_attr_t     attrTest;
pthread_t          threadTest;
struct sched_param paramsTest;

pthread_attr_t     attrStarter;
pthread_t          threadStarter;
struct sched_param paramsStarter;

sem_t semServiceTest;
pthread_mutex_t mutexTest;
unsigned int sequencePeriods = 30;
unsigned int remainingSequencePeriods;


static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int schedulingPolicy,
                                   int priority,
                                   int affinity );
static void *starter( void *threadp );
void sequencer( int id );
static void *service_test( void *threadp );
static void print_scheduler(void);

/******************************************************************************
 *
 * run_test_services
 *
 *
 * Description: Starts the starter thread.
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

    // Start the launger and wait for it to join back.
    // Starts the test service and the scheduler
    rv = pthread_create( &threadStarter, // pointer to thread descriptor
                         &attrStarter,   // use specific attributes
                         starter,         // thread function entry point
                         NULL );          // parameters to pass in
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
 * Description: initializes mutexes and semaphores needed by the sequencer and
 *              the test service. Then starts the test the service and the
 *              sequencer. The sequencer timer is also started here.
 *
 * Arguments:   threadp (IN): not used.
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
    bool isSemCreated = false;
    bool isMutCreated = false;

    printf( "starter " );
    print_scheduler( );

    if ( sem_init( &semServiceTest, 0, 0 ) != 0 )
    {
        errno_print( "semaphore init" );
        goto END;
    }
    isSemCreated = true;

    if ( pthread_mutex_init(&mutexTest, NULL) != 0 )
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

    // set "test" service thread attributes
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
    
    rv =  pthread_create(&threadTest, // pointer to thread descriptor
                         &attrTest,   // use specific attributes
                         service_test, // thread function entry point
                         NULL );       // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating test service\n" );
        goto END;
    }

    /* set up to signal SIGALRM if timer expires */
    remainingSequencePeriods = sequencePeriods;
    timer_create( CLOCK_REALTIME, NULL, &sequencerTimer );
    signal(SIGALRM, (void(*)()) sequencer);

    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = 100000000; // refill timer with this
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = 1; // initial value. start ASAP.

    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );

    rv = pthread_join( threadTest, NULL );
    if ( rv < 0)
    {
        fprintf(stderr, "error joining with test service\n");
    }

    intervalTime.it_interval.tv_sec = 0;
    intervalTime.it_interval.tv_nsec = 0;
    intervalTime.it_value.tv_sec = 0;
    intervalTime.it_value.tv_nsec = 0;
    timer_settime( sequencerTimer, flags, &intervalTime, &oldIntervalTime );

END:

    if ( isSemCreated == true ) sem_destroy( &semServiceTest );
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
    printf( "in the SE-SE-SEQUENCER!!\n" );
    pthread_mutex_lock( &mutexTest );
    if ( remainingSequencePeriods > 0 ) remainingSequencePeriods--;
    pthread_mutex_unlock( &mutexTest );
    sem_post( &semServiceTest );
}


/******************************************************************************
 *
 * service_test
 *
 *
 * Description: runs the tests.
 *
 * Arguments:   threadp (IN): not used.
 *
 * Return:      Null pointer
 *
 *****************************************************************************/
static void *service_test( void *threadp )
{
    bool isFinal = false;

    while ( true )
    {
        sem_wait( &semServiceTest );
        printf( "in the testing function!!!!\n" );
        pthread_mutex_lock( &mutexTest );
        if ( remainingSequencePeriods == 0 ) isFinal = true;
        pthread_mutex_unlock( &mutexTest );
        if ( isFinal == true ) break;
    }

    return (void *)NULL;
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
