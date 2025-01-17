#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <list>
#include <thread>
#include "Util.h"
#include "SnapcastComm.h"
using namespace std;

#define SNAPCASTPLAYERSOCKET "./socketForSnapcastPlayer"
#define LIGHTNINGCAST_COMM_HEADER_KEY         0x9876

// Function to handle Snapcast communication
void SnapcastComm::handle()
{
    std::unique_lock<std::mutex> lock(mutex);
    while(true)
    {
        switch (cmd) 
        {
        case SNAPCASTThreadCmd::SNAPCASTThreadCmd_OPEN:
            lock.unlock();
            do_open();
            lock.lock();
            break;
        case SNAPCASTThreadCmd::SNAPCASTThreadCmd_CLOSE:
            finishCmd();
            break;
        case SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE:
            cond.wait(lock, [&] { return ready; });
            ready = false;
            break;
        case SNAPCASTThreadCmd::SNAPCASTThreadCmd_EXIT:
            finishCmd();
            return;
        }
    }
}

// Function to open Snapcast communication
void SnapcastComm::Open()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = SNAPCASTThreadCmd::SNAPCASTThreadCmd_OPEN;
    ready = true;
    ready_sync = false;
    cond.notify_one();
    while (cmd != SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE)
    {
        cond_sync.wait(lock, [&] { return ready_sync; });
    }
}

// Function to close Snapcast communication
void SnapcastComm::Close()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = SNAPCASTThreadCmd::SNAPCASTThreadCmd_CLOSE;
    ready = true;
    ready_sync = false;
    cond.notify_one();
    // Wake up wake_fd
    wake_fd.Write();
    while (cmd != SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE)
    {
        cond_sync.wait(lock, [&] { return ready_sync; });
    }
}

// Function to finish a command
void SnapcastComm::finishCmd()
{
    cmd = SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE;
    ready_sync = true;
    cond_sync.notify_one();
}

// Function to finish a command with a lock
void SnapcastComm::LockfinishCmd()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE;
    ready_sync = true;
    cond_sync.notify_one();
}

// Function to exit Snapcast communication
void SnapcastComm::Exit()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = SNAPCASTThreadCmd::SNAPCASTThreadCmd_EXIT;
    ready = true;
    ready_sync = false;
    cond.notify_one();
}

// Function to connect to the server
void SnapcastComm::connectServer()
{
    struct sockaddr_un server;
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("opening stream socket");
        exit(1);
    }
    server.sun_family = AF_UNIX;
    strcpy(server.sun_path, SNAPCASTPLAYERSOCKET);
    while(1)
    {
        if (connect(server_fd, (struct sockaddr *) &server, sizeof(struct sockaddr_un)) == 0)
        {
            break;
        }
        perror("connecting stream socket");
        sleep(1);
    }
}

// Function to open Snapcast communication
void SnapcastComm::do_open()
{
    bool rc = true;
    // Keep trying to connect to the server
    connectServer();
    LockfinishCmd();
    fd_set read_fds,master;
    FD_ZERO(&read_fds);
    FD_ZERO(&master);
    FD_SET(server_fd, &master);
    FD_SET(wake_fd.Get(), &master);
    int fdmax = server_fd > wake_fd.Get() ? server_fd : wake_fd.Get();
    int i;
    bool break_while = false;
    while(cmd == SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE)
    {
        read_fds = master;
        if(select(fdmax+1, &read_fds, NULL, NULL, NULL) <= 0)
        {
            break;
        }
        for(i = 0; i <= fdmax; i++)
        {
            if(FD_ISSET(i, &read_fds))
            {
                if (i == wake_fd.Get())
                {
                    wake_fd.Read();
                    if(cmd != SNAPCASTThreadCmd::SNAPCASTThreadCmd_NONE)
                    {
                        break;
                    }
                }
                else if(i == server_fd)
                {
                    rc = doRead(i);
                    if(!rc)
                    {
                        break_while = true;
                        break;
                    }
                }
            }
        }
        if(break_while)
        {
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(server_mutex);
        // Close the connection
        if(server_fd != -1)
        {
            shutdown(server_fd, SHUT_RDWR);
            close(server_fd);
            server_fd = -1;
        }
    }
}

// Function to read data from the server
bool SnapcastComm::doRead(int fd)
{
    ssize_t result;
    lightningCast_header hi;

    // First receive the header
    memset(&hi, 0x00, sizeof(lightningCast_header));
    uint8_t *hi_buf = (uint8_t *)&hi;
    size_t hi_length = 0;
    // Receive the actual data until sizeof(lightningCast_header) is reached
    while (hi_length < sizeof(lightningCast_header))
    {
        result = read(fd, hi_buf + hi_length, sizeof(lightningCast_header) - hi_length);
        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
            perror("read < 0");
            return false;
        }
        else if(result == 0)
        {
            return false;
        }
        hi_length += result;
    }
    if (ntohl(hi.key) != (uint32_t)LIGHTNINGCAST_COMM_HEADER_KEY)
    {
        return false;
    }
    hi.data_size = ntohl(hi.data_size);
    std::unique_ptr<uint8_t[]> buffer;
    size_t bufferSize = hi.data_size;
    buffer.reset(new uint8_t[bufferSize]);
    uint8_t *buf = buffer.get();
    size_t length = 0;
    while (length < (size_t)hi.data_size) {
        result = read(fd, buf + length, hi.data_size - length);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return false;
        } else if (result == 0) {
            return false;
        }
        length += result;
    }
    return doParse(fd, buf, length);
}

// The protocol is divided into two parts: the first part consists of size + data; if the cmd of the first part is TuneIn_REPORT_DATA, the second part consists of data, otherwise there is no second part.
bool SnapcastComm::doParse(const int fd, const uint8_t *buf, size_t length)
{
    // First part head
    lightningCast_header hi;
    memset(&hi, 0x00, sizeof(lightningCast_header));
    memcpy(&hi, buf, sizeof(lightningCast_header));
    if(ntohl(hi.key) != (uint32_t)LIGHTNINGCAST_COMM_HEADER_KEY)
    {
        return false;
    }
    // First part size
    hi.data_size = ntohl(hi.data_size);
    int one_size = hi.data_size;
    // Parse the first part of the data, which is in JSON format
    Json::Reader reader;
    Json::Value root;
    string one_data((const char *)buf+sizeof(lightningCast_header), one_size);
    if (!reader.parse(one_data, root, false))
    {
        return false;
    }
    ssize_t second_size = 0;
    LIGHTNINGCASTCLIENTCmd cmd = (LIGHTNINGCASTCLIENTCmd)root["Cmd"].asInt();
    if(cmd != LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_REPORT_DATA)
    {
        // DEBUG("one_data=%s",one_data.c_str());
    }
    long alsa_avail = 0;
    long alsa_delay = 0;
    int alsa_result = 0;
    switch (cmd)
    {
    case LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_START:
        // Wake up the thread that sent the play command
        send_start_result = root["Result"].asInt() == 1 ? true : false;
        LockSignal();
        // tuneIn_file = fopen("/var/log/1.flac", "wb+");
        break;
    case LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_STOP:
        // Wake up the thread that sent the stop command
        LockSignal();
        break;
    default:
        return false;
    }
    return true;
}

// First send the verification code and size, then send the actual data
bool SnapcastComm::processSend(const string &str)
{
    std::lock_guard<std::mutex> lock(server_mutex);
    if(server_fd == -1)
    {
        return false;
    }
    lightningCast_header hi;
    memset(&hi, 0x00, sizeof(lightningCast_header));
    hi.key = htonl(LIGHTNINGCAST_COMM_HEADER_KEY);
    hi.data_size = htonl(str.size());

    std::unique_ptr<uint8_t[]> sendBuf;
    size_t bufferSize = sizeof(lightningCast_header)+str.size();
    sendBuf.reset(new uint8_t[bufferSize]);
    uint8_t *buf = sendBuf.get();
    memcpy(buf, &hi, sizeof(lightningCast_header));
    memcpy(buf+sizeof(lightningCast_header), str.c_str(), str.size());

    size_t bytes_left = bufferSize;
    ssize_t written_bytes = 0;
    uint8_t *ptr = sendBuf.get();
    while(bytes_left > 0)
    {
        written_bytes = ::write(server_fd, ptr, bytes_left);
        if (written_bytes < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                perror("send too fast , try again.");
                continue;
            }
            else
            {
                perror("send() from peer error");
                break;
            }
        }
        else if (written_bytes == 0)
        {
            break;
        }
        bytes_left -= written_bytes;
        ptr += written_bytes;
    }
    return true;
}

bool SnapcastComm::sendStart(const std::string &snapserver_ipaddr, const int sample_rate, const int format, const int channels)
{
    send_start_result = false;
    // Protocol format: "[{"Cmd"xxx, "Url":""}]"
    // Pack the data
    Json::Value rootS;
    Json::FastWriter writer;
    rootS["Cmd"] = (int)LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_START;
    rootS["SnapServerIPAddr"] = snapserver_ipaddr;
    string json_str = writer.write(rootS);
    if(!processSend(json_str))
    {
        return false;
    }
    // Wait for lightningcastclient to send a receipt
    LockWait();
    return send_start_result;
}

bool SnapcastComm::sendPause()
{
    // Pack the data
    Json::Value rootS;
    Json::FastWriter writer;
    rootS["Cmd"] = (int)LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_STOP;
    string json_str = writer.write(rootS);
    if(!processSend(json_str))
    {
        return false;
    }
    // Wait for lightningcastclient to send a receipt
    LockWait();
    return true;
}

bool SnapcastComm::sendStop()
{
    // Pack the data
    Json::Value rootS;
    Json::FastWriter writer;
    rootS["Cmd"] = (int)LIGHTNINGCASTCLIENTCmd::LIGHTNINGCASTClientCmd_STOP;
    string json_str = writer.write(rootS);
    if(!processSend(json_str))
    {
        return false;
    }
    // Wait for lightningcastclient to send a receipt
    LockWait();
    return true;
}
