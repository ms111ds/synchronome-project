#include "Libraries/common_library.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "Libraries/v4l2_library.h"
#include "Services/synchronome_services.h"


struct v4l2_state cameraState;

int main( int argc, char *argv[] )
{
    bool isPass;
    char *deviceName;

    if( ( argc > 1) && ( argv[1][0] == '/' ) )
        deviceName = argv[1];
    else
        deviceName = "/dev/video0";


    init_v4l2_state( &cameraState );
    
    isPass = open_device( deviceName, &cameraState );
    if ( isPass == false )
    {
        printf("open_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }

    isPass = init_device( IO_METHOD_MMAP, HRES, VRES, &cameraState );
    if ( isPass == false )
    {
        printf("init_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    isPass = start_capturing( &cameraState );
    if ( isPass == false )
    {
        printf("start_captureing return: %i\n", isPass );
    }

    printf ("state address 1 %p\n", &cameraState );
    isPass = run_synchronome( &cameraState );
    if ( isPass == false )
    {
        printf("run_test_services return: %i\n", isPass );
    }

    isPass = stop_capturing( &cameraState );
    if ( isPass == false )
    {
        printf("stop_captureing return: %i\n", isPass );     
    }
    
    isPass = uninit_device( &cameraState );
    if ( isPass == false )
    {
        printf("uninit_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    isPass = close_device( &cameraState );
    if ( isPass == false )
    {
        printf("close_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

