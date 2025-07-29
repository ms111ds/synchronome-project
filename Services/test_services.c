#include "test_services.h"

#ifndef _TEST_SRV_C
#define _TEST_SRV_C

#define CORE_0 0
#define CORE_1 1
#define CORE_2 2
#define CORE_3 3

pthread_attr_t     attr_test;
pthread_t          thread_test;
struct sched_param params_test;

pthread_attr_t     attr_starter;
pthread_t          thread_starter;
struct sched_param params_starter;


static bool set_thread_attributes( pthread_attr_t *attr,
                                   struct sched_param *params,
                                   int scheduling_policy,
                                   int priority,
                                   int affinity );
static void *starter( void *threadp );
static void *service_test( void *threadp );
static void print_scheduler(void);

/******************************************************************************
 *
 * mainloop
 *
 *
 * Description: Sets up the scheduling attributes for the scheduler, the
 *              frame load service, and frame processing service. Then
 *              starts the scheduler and waits for it to finish.
 *
 * Arguments:   N/A
 *
 * Return:      N/A
 *
 *****************************************************************************/
bool run_test_services( struct v4l2_state *state )
{
    int thread_max_priority;
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
    thread_max_priority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( thread_max_priority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }

    // set "test" service thread attributes
    isPass = set_thread_attributes( &attr_starter,
                                    &params_starter,
                                    MY_SCHED_POLICY,
                                    thread_max_priority,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: starter service\n");
        goto END;
    }

    // Start the launger and wait for it to join back.
    // Starts the test service and the scheduler
    rv = pthread_create( &thread_starter, // pointer to thread descriptor
                         &attr_starter,   // use specific attributes
                         starter,         // thread function entry point
                         NULL );          // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating starter service\n" );
        goto END;
    }

    rv = pthread_join( thread_starter, NULL );
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
 *              scheduling_policy (IN): scheduling policy to set up in attr.
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
                                   int scheduling_policy,
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

    rc = pthread_attr_setschedpolicy( attr, scheduling_policy );
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
 * Description: starts the image (frame) loading service and image (frame)
 *              processing service.
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
    int thread_max_priority;

    printf( "starter " );
    print_scheduler( );

    thread_max_priority = sched_get_priority_max( MY_SCHED_POLICY );
    if ( thread_max_priority == -1 )
    {
        errno_print("error getting minimum priority");
        goto END;
    }

    // set "test" service thread attributes
    isPass = set_thread_attributes( &attr_test,
                                    &params_test,
                                    MY_SCHED_POLICY,
                                    thread_max_priority,
                                    CORE_3 );
    if ( isPass != true )
    {
        fprintf(stderr, "set_thread_attributes: starter service\n");
        goto END;
    }
    
    rv =  pthread_create(&thread_test, // pointer to thread descriptor
                         &attr_test,   // use specific attributes
                         service_test, // thread function entry point
                         NULL );       // parameters to pass in
    if ( rv < 0 )
    {
        errno_print( "error creating test service\n" );
        goto END;
    }



    rv = pthread_join( thread_test, NULL );
    if ( rv < 0)
    {
        fprintf(stderr, "error joining with test service\n");
    }

END:

    return (void *)NULL;
}

static void *service_test( void *threadp )
{
    printf( "in the testing function!!!!\n" );

    return (void *)NULL;
}




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
