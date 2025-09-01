INCLUDE_DIRS = ./Libraries ./Services
LIB_DIRS = 
CC=gcc

CDEFS=
CFLAGS= -Wall -O0 -g $(addprefix -iquote ,$(INCLUDE_DIRS)) $(CDEFS)
#CFLAGS= -Wall -O0 -pg $(addprefix -iquote ,$(INCLUDE_DIRS)) $(CDEFS)
LIBS= -lrt

HFILES= 
CFILES= synchronome_tests.c

SRCS= ${HFILES} ${CFILES}
OBJS= ${CFILES:.c=.o}
CLEAN_EXT = .o .d .ppm .pgm ~
CE = $(addprefix -iquote ,$(INCLUDE_DIRS))

.PHONY: all clean remake

all: synchronome_program


clean:
	find ./ -type f \( -name "*.o" -o \
	                   -name "*.d" -o \
	                   -name "*.ppm" -o \
	                   -name "*.pgm" -o \
	                   -name "*.out" -o \
	                   -name "*~" \) -delete
	-rm -f synchronome_program

remake: clean all

common_library.o: ./Libraries/common_library.c
	$(CC) $(CFLAGS) -c $<

v4l2_library.o: ./Libraries/v4l2_library.c common_library.o
	$(CC) $(CFLAGS) -c $<
	
synchronome_services.o: ./Services/synchronome_services.c common_library.o
	$(CC) $(CFLAGS) -c $<
	
synchronome_program.o: synchronome_program.c common_library.o
	$(CC) $(CFLAGS) -c $<
	
synchronome_program: synchronome_program.o v4l2_library.o synchronome_services.o common_library.o
	$(CC) $(LDFLAGS) $(CFLAGS) $^ -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<
