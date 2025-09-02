
The purpose of this software is to automatically take single pictures of each tick of a clock or timer. It has two modes of operation.

* 1  Hz - to take pictures of an analog clock, each time the second hand ticks.

* 10 Hz - to take pictures of a digital timer, each time the 1/10th of a second digit changes. Note that the 1/10th of a second digit is assumed to be the smallest unit of time displayed by the timer.

IMPORTANT NOTE: The software has only been tested with low resolution images, i.e. 960 pixels by 540 pixels, and probably will only work correctly with those.

This program was was developed for use in a course. Much of the code has been borrowed from https://github.com/siewertsmooc/RTES-ECEE-5623. Thank you Mr. Siewert.

Requirements (Hardware)
---------------------------------------------------------------
* Quad core processor
* UVC camera that can capture images at 60 frames per second or faster
* Analog clock with seconds hand or digital timer with resolution of 1/10th of a second.

Requirements (Software)
---------------------------------------------------------------
* Linux OS
* V4L2 support
* gcc compiler
* make
* another program to test the camera is working (e.g. VLC media player).

Building the software
--------------------------------------------------------------
1) Open a command-line window to the main folder (should have the makefile in there too)
2) Enter the command "make remake"


Running the software
--------------------------------------------------------------
1) First make sure the setup is correct.
    * Ensure the camera is pointed towards clock or timer.
    * Use other software to check that the image of the clock or timer is both LARGE and CLEAR. A program such as VLC can help test this out.
    * Modify the horizontal and vertical resolution 
2) Build the software if it hasn't been built already (please see above).
3) Enter the following command into the command-line window **sudo ./synchronome_program**.
    * If the camera is not on /dev/video0, use the command **sudo ./synchronome_program <camera device path>**.
   E.g. if camera on /dev/video1 the command will be "sudo ./synchronome_program /dev/video1"
4) Wait about 3 minutes for the software to finish. If not, press ctrl+c to exit.
5) The software probably wont run correctly the first time around as it probably needs to be
   calibrated to the camera positioning and clock. More on this below.

Operating options
--------------------------------------------------------------
Operating options are chosen in ./Services/synchronome_services.h. The program must be built again each time the options are changed.
* For the LOGGING and FEATURES options: The #define values must be set to "1"
* For the FUNCTIONAL options, an appropriate value must be provided. The only FUNCTIONAL option that is safe to change is NUM_PICS, changing one of the others may break the program.

Below is a summary of the most important operating options.
* DUMP_DIFFS: used in synchronome calibration. Will dump a value known as a "diff" to the Linux system log, e.g.
              syslog or journalctl. The dumped values can be compared to other dumped values to determine a correct
              threshold to set. The threshold will be talked about below.
* OPERATE_AT_10_HZ: Set to 1 for operation on a 10Hz digital timer. Set to 0 for operation on a 1Hz analog clock
                    (i.e. 1Hz tick of the seconds hand).
* USE_COOL_BORDER: Set to 1 to add a blue border to the image. Set to 0 to disable the border.
* OUTPUT_YUYV_PPM: Set to 1 keep the images in YUYV format, making them appear distorted. Set to 0 transform the
                   images to RGB format for normal viewing.
* NUM_PICS: The number of distinct tick images that need to be saved. Once this many images are saved, the program ends.

Program calibration
--------------------------------------------------------------
Follow the instructions in "Running the software" to execute an initial run. The option DUMP_DIFFS should be turned on
by default. Then do the following:

1) Check the syslog ( or journalctl ) for the entries produced by this program these should have the string "[Final Project]" and "diff" which can be searched for with the shell grep command.

2) See which entries have a much higher diff than the others. These show when the clock ticks are occuring. Ensure
   these are either around 1 second (for 1Hz) or 100 milliseconds (for 10Hz) apart. Try to find some intermediate
   value as a threshold, i.e. some value that:
   * is high enough that the diffs between the ticks are BELOW this value.
   * is low enough that even diffs produced by somewhat small tick movements are ABOVE this value. These relatively small diffs on ticks can be caused by multiple images being captured during a single tick.

3) Once the threshold is decided upon, update the #define DIFF_THRESHOLD in ./Services/synchronome_services.c. Note that there are two of these, one for the 1 Hz and another for the 10 Hz, so update the approprate one.

4) Compile and run again. Check the syslogs for the diffs again if there is another issue. A new threshold may need to be narrowed down to. Also check the following.
   * That the clock/timer is large enough and has sufficient contrast with the background. Also with timers, ensure
     each digit is sufficiently different from the others.
   * That the camera is actually capturing a large and clear image of the clock/timer. Use another program, like VLC,
     for this.

5) Feel free to play around with the diff calculation algorithm calc_array_diff_8bit() if nothing else works.
   It is found in ./Services/synchronome_services.c
