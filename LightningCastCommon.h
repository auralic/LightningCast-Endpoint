#ifndef _LIGHTNINGCASTCOMMON_H
#define _LIGHTNINGCASTCOMMON_H

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

#define LIGHTNINGCASTTCPPORT 7700
#define LIGHTNINGCONFIGTCPPORT 7701
#define LIGHTNINGCAST_COMM_HEADER_KEY         0x9876

// Structure for LightningCast header
struct lightningCast_header
{
    uint32_t   key;        // Authentication code for both parties
    uint32_t   data_size;  // Number of bytes to be transmitted, excluding the 8-byte header
    lightningCast_header():key(0),data_size(0){}
    void clear()
    {
        key = 0;
        data_size = 0;
    }
};

#endif
