#ifndef _LIGHTNINGCASTSERVER_H
#define _LIGHTNINGCASTSERVER_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include "LightningCastClient.h"
#include "tinysvcmdns.h"

// ServicePublisher class definition
class ServicePublisher {
   public:
    ServicePublisher() : stop_thread(false) {}
    ~ServicePublisher() {}

    // Method to handle service publishing
    void handle();
    // Method to register mDNS service using tinysvcmdns
    int mdns_tinysvcmdns_register(
        const char *ap1name, __attribute__((unused)) char *ap2name, int port,
        __attribute__((unused)) char **txt_records,
        __attribute__((unused)) char **secondary_txt_records);
    // Method to unregister mDNS service using tinysvcmdns
    void mdns_tinysvcmdns_unregister(void);

    struct mdnsd *svr; // mDNS server
    std::string service_name; // Service name
    std::atomic<bool> stop_thread; // Flag to stop the thread
};

// Enum for LightningCast handshake commands
enum class LightningCastHandshakeCmd {
    LightningCastHandshakeCmd_NONE = 0,
    LightningCastHandshakeCmd_OPEN,
    LightningCastHandshakeCmd_CLOSE
};

// LightningCastServer class definition
class LightningCastServer {
   public:
    LightningCastServer(LightningCastClient &_lightningCastClient) : lightningCastClient(_lightningCastClient) {}
    ~LightningCastServer() {}
    LightningCastClient &lightningCastClient; // Reference to LightningCastClient
    EventFD wake_fd; // Event file descriptor for wake-up
    // Method to handle server operations
    void handle();
    // Method to read data from client
    bool doRead(const int client_fd);
    // Method to parse data from client
    bool doParse(const int client_fd, const uint8_t *buf, const size_t length);
    // Method to send data to client
    void processSend(const int client_fd, const std::string &str);
    // Method to open connection with client
    void Open(const int client_fd, const std::string &ip_addr);
    // Method to exit the server
    void Exit();
};

#endif
