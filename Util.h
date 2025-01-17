#ifndef _UTIL_H
#define _UTIL_H

#include <string>
#include <iostream>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/eventfd.h>
#include <sys/time.h>

// Enum for LightningCast client commands
enum class LIGHTNINGCASTCLIENTCmd
{
    LIGHTNINGCASTClientCmd_NONE = 0,
    LIGHTNINGCASTClientCmd_START,
    LIGHTNINGCASTClientCmd_STOP,
    LIGHTNINGCASTClientCmd_REPORT_DATA
};

// Function to make a socket non-blocking
int make_socket_non_blocking(int sfd);

// Function to calculate the difference in microseconds between two timespec structures
uint64_t diff_in_us(struct timespec t1, struct timespec t2);

// Function to get the current time as a string
void getTimeNowStr(char *s);

// Macro for debugging
#define DEBUG(format,...)
// #define DEBUG(format,...) {char timeStr[100]={0};getTimeNowStr(timeStr);pid_t pid = gettid();printf("[%s]:[%d] - File: " __FILE__ ", Line: %05d: " format "\n", timeStr,pid, __LINE__, ##__VA_ARGS__);}

// Class for event file descriptor
class EventFD {
    int fd;

public:
    EventFD();
    ~EventFD();

    int Get() const {
        return fd;
    }

    bool Read();

    void Write();
};

#endif
