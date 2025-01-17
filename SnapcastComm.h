#ifndef _SNAPCASTHANDLER_H
#define _SNAPCASTHANDLER_H

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
#include <functional>
#include <mutex>
#include <condition_variable>
#include "json/json.h"
#include "Util.h"
#include "LightningCastCommon.h"

// Enum for Snapcast thread commands
enum class SNAPCASTThreadCmd
{
    SNAPCASTThreadCmd_NONE = 0,
    SNAPCASTThreadCmd_OPEN,
    SNAPCASTThreadCmd_CLOSE,
    SNAPCASTThreadCmd_EXIT
};

// Class for Snapcast communication
class SnapcastComm
{
public:
    SNAPCASTThreadCmd cmd;
    std::mutex mutex;
    std::condition_variable cond;
    std::condition_variable cond_sync;
    bool ready;
    bool ready_sync;
    std::mutex server_mutex;
    int server_fd;
    EventFD wake_fd;
    std::string current_snapserver_ipaddr;
    bool send_start_result;
    bool pending;

    // Function to wait for a lock
    void LockWait() {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]() { return pending; });
        pending = false;
    }

    // Function to signal a lock
    void LockSignal() {
        {
            std::unique_lock<std::mutex> lock(mutex);
            pending = true;
        }
        cond.notify_one();
    }

    SnapcastComm() : cmd(SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE),ready(false),ready_sync(false),server_fd(-1),send_start_result(false),pending(false)
    {
    }
    ~SnapcastComm()
    {
    }

    void handle();
    void do_open();
    void finishCmd();
    void LockfinishCmd();
    void Open();
    void Close();
    void connectServer();
    void Exit();

    bool doRead(int fd);
    bool doParse(const int fd, const uint8_t *buf, size_t length);
    bool processSend(const std::string &str);
    bool sendStart(const std::string &snapserver_ipaddr, const int sample_rate, const int format, const int channels);
    bool sendPause();
    bool sendStop();
};

#endif
