#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include "Util.h"

// Function to make a socket non-blocking
int make_socket_non_blocking(int sfd)
{
    int flags, s;

    flags = fcntl( sfd, F_GETFL, 0 );
    if ( flags == -1 )
    {
        perror( "fcntl" );
        return(-1);
    }

    flags   |= O_NONBLOCK;
    s   = fcntl( sfd, F_SETFL, flags );
    if ( s == -1 )
    {
        perror( "fcntl" );
        return(-1);
    }

    return(0);
}

// Function to calculate the difference in microseconds between two timespec structures
uint64_t diff_in_us(struct timespec t1, struct timespec t2)
{
    struct timespec diff;
    if (t2.tv_nsec-t1.tv_nsec < 0) {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec - 1;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec + 1000000000;
    } else {
        diff.tv_sec  = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }
    return (diff.tv_sec * 1000000.0 + diff.tv_nsec / 1000.0);
}

// Function to get the current time as a string
void getTimeNowStr(char *s)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t currentTime = tv.tv_sec;
    
    struct tm systime;
    localtime_r(&currentTime, &systime);

    sprintf(s, "%04d-%02d-%02d %02d:%02d:%02d:%06ld",
        systime.tm_year + 1900,
        systime.tm_mon + 1,
        systime.tm_mday,
        systime.tm_hour,
        systime.tm_min,
        systime.tm_sec,
        tv.tv_usec);
}

// Constructor for EventFD class
EventFD::EventFD() :fd(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
    if (fd < 0)
    {
        printf("eventfd() failed\n");
    }
}

// Destructor for EventFD class
EventFD::~EventFD()
{
    close(fd);
}

// Function to read from the event file descriptor
bool EventFD::Read()
{
    eventfd_t value;
    return read(fd, &value, sizeof(value)) == (ssize_t)sizeof(value);
}

// Function to write to the event file descriptor
void EventFD::Write()
{
    static constexpr eventfd_t value = 1;
    ssize_t nbytes = write(fd, &value, sizeof(value));
    if(nbytes == -1)
    {
       printf("EventFD::Write error\n");
    }
}
