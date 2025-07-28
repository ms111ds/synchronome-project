#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "Libraries/v4l2_library.h"
#include "Libraries/project_types.h"

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
    
    isPass = close_device( &cameraState );
    if ( isPass == false )
    {
        printf("close_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

