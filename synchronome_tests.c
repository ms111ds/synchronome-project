#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "Libraries/v4l2_library.h"
#include "Libraries/project_types.h"


int main( int argc, char *argv[] )
{
    int fileDescriptor;
    bool isPass;

    isPass = open_device( &fileDescriptor );
    if ( isPass == false )
    {
        printf("open_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    isPass = close_device( &fileDescriptor );
    if ( isPass == false )
    {
        printf("close_device return: %i\n", isPass );
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

