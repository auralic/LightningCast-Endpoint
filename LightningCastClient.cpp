#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <map>
#include <sstream>
#include <sys/time.h>
#include "Util.h"
#include "json/json.h"
#include "LightningCastClient.h"
using namespace std;

#define LIGHTNINGCAST_VERSION "1.0"

// Function to handle LightningCast client operations
void LightningCastClient::handle()
{
    std::unique_lock<std::mutex> lock(mutex);
    while(true)
    {
        switch (cmd) 
        {
        case LightningCastClientCmd::LightningCastServerCmd_OPEN:
            lock.unlock();
            do_open();
            lock.lock();
            break;
        case LightningCastClientCmd::LightningCastServerCmd_CLOSE:
            finishCmd();
            break;
        case LightningCastClientCmd::LightningCastServerCmd_NONE:
            cond.wait(lock, [&] { return ready; });
            ready = false;
            break;
        case LightningCastClientCmd::LightningCastServerCmd_EXIT:
            finishCmd();
            return;
        }
    }
}

// Function to open LightningCast client
bool LightningCastClient::Open(std::string _serverIP)
{
    if(getIsOpen())
    {
        doStop();
        Close();
    }
    // Initiated by LightningCastServer
    std::unique_lock<std::mutex> lock(mutex);
    cmd = LightningCastClientCmd::LightningCastServerCmd_OPEN;
    is_open = false;
    serverIP = _serverIP;
    DEBUG("Open serverIP=%s",serverIP.c_str());
    ready = true;
    ready_sync = false;
    cond.notify_one();
    while (cmd != LightningCastClientCmd::LightningCastServerCmd_NONE)
    {
        cond_sync.wait(lock, [&] { return ready_sync; });
    }
    return is_open;
}

// Function to close LightningCast client
void LightningCastClient::Close()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = LightningCastClientCmd::LightningCastServerCmd_CLOSE;
    ready = true;
    ready_sync = false;
    cond.notify_one();
    // Wake up wake_fd
    wake_fd.Write();
    while (cmd != LightningCastClientCmd::LightningCastServerCmd_NONE)
    {
        cond_sync.wait(lock, [&] { return ready_sync; });
    }
    is_open = false;
}

// Function to finish a command
void LightningCastClient::finishCmd()
{
    cmd = LightningCastClientCmd::LightningCastServerCmd_NONE;
    ready_sync = true;
    cond_sync.notify_one();
}

// Function to finish a command with a lock
void LightningCastClient::LockfinishCmd()
{
    std::unique_lock<std::mutex> lock(mutex);
    cmd = LightningCastClientCmd::LightningCastServerCmd_NONE;
    ready_sync = true;
    cond_sync.notify_one();
}

// Function to exit LightningCast client
void LightningCastClient::Exit()
{
    if(getIsOpen())
    {
        doStop();
        Close();
    }
    std::unique_lock<std::mutex> lock(mutex);
    cmd = LightningCastClientCmd::LightningCastServerCmd_EXIT;
    ready = true;
    ready_sync = false;
    cond.notify_one();
}

// Function to connect to the server
bool LightningCastClient::connectServer()
{
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("opening sock faild\n");
        return false;
    }
    struct hostent *hp = NULL;
    hp = gethostbyname(serverIP.c_str());
    if (hp == NULL)
    {
        close(server_fd);
        server_fd = -1;
        return false;
    }
    struct sockaddr_in sa;
    memset(&sa, 0x00, sizeof(struct sockaddr_in));
    memcpy((char *)&sa.sin_addr, hp->h_addr, hp->h_length);
    sa.sin_family = hp->h_addrtype;
    sa.sin_port = htons((uint16_t)LIGHTNINGCASTTCPPORT);
    bool rc = false;
    if(connect(server_fd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) == 0)
    {
        if(make_socket_non_blocking(server_fd) == 0)
        {
            rc = true;
        }
    }
    if(!rc)
    {
        close(server_fd);
        server_fd = -1;
    }
    return rc;
}

// Function to open LightningCast client
void LightningCastClient::do_open()
{
    bool rc = true;
    if(!connectServer())
    {
        setIsOpen(false);
        LockfinishCmd();
    }
    else
    {
        setIsOpen(true);
        LockfinishCmd();
        sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VERSION);
        sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VOLUME_CONTROL, 1);
        sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VOLUME, current_volume);
        sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_MUTE, 0);
        clock_gettime(CLOCK_MONOTONIC, &aliveTime);
        fd_set read_fds,master;
        FD_ZERO(&read_fds);
        FD_ZERO(&master);
        FD_SET(server_fd, &master);
        FD_SET(wake_fd.Get(), &master);
        int fdmax = server_fd > wake_fd.Get() ? server_fd : wake_fd.Get();
        int i;
        bool break_while = false;
        // Set timeout to 1 second
        struct timeval timeout;
        while(cmd == LightningCastClientCmd::LightningCastServerCmd_NONE)
        {
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000;
            read_fds = master;
            int ready = select(fdmax+1, &read_fds, NULL, NULL, &timeout);
            if(ready == -1)
            {
                break;
            }
            else if(ready == 0)
            {
                //timeout
                sendAlive();
                if(!checkCastServerAlive())
                {
                    break;
                }
            }
            else
            {
                for(i = 0; i <= fdmax; i++)
                {
                    if(FD_ISSET(i, &read_fds))
                    {
                        if (i == wake_fd.Get())
                        {
                            wake_fd.Read();
                            if(cmd != LightningCastClientCmd::LightningCastServerCmd_NONE)
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
        }
        setIsOpen(false);
        // Stop snapcast
        doStop();
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
}

// Function to read data from the server
bool LightningCastClient::doRead(int fd)
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

// Function to parse the received data
bool LightningCastClient::doParse(const int fd, const uint8_t *buf, size_t length)
{
    // Parse the first part of the data, which is in JSON format
    Json::Reader reader;
    Json::Value root;
    string one_data((const char *)buf, length);
    if (!reader.parse(one_data, root, false))
    {
        return false;
    }

    LightningCastCommCmd cmd = (LightningCastCommCmd)root["Cmd"].asInt();
    if((cmd != LightningCastCommCmd::LightningCastCommCmdCmd_SET_POSITION) &&
       (cmd != LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_ALIVE))
    {
        DEBUG("doParse length=%lu one_data=%s", length, one_data.c_str());
    }

    switch (cmd)
    {
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_VERSION:
        doSetLightningcastVersion(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_METADATA:
        sendAlive();    // If cmd is setmetadata, send a heartbeat packet upon receipt, because for example, QQ will continuously send metadata, preventing Android from continuously sending messages for more than 5 seconds, and here not sending a heartbeat packet will cause Android to think that no heartbeat packet has been received for 5 seconds and offline this device
        doSetMetadata(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_AUDIOFORMAT:
        doSetAudioFormat(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_DURATION:
        doSetDuration(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_POSITION:
        doSetPosition(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_STATUS:
        doSetStatus(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_PLAY:
        doPlay(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_PAUSE:
        doPause();
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_STOP:
        doStop();
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_VOLUME:
        sendAlive();    // If cmd is setvolume, send a heartbeat packet upon receipt, preventing Android from continuously sending messages for more than 5 seconds, and here not sending a heartbeat packet will cause Android to think that no heartbeat packet has been received for 5 seconds and offline this device
        doSetVolume(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_MUTE:
        doSetMute(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_STEP_VOLUME:
        sendAlive();    // If cmd is setstepvolume, send a heartbeat packet upon receipt, preventing Android from continuously sending messages for more than 5 seconds, and here not sending a heartbeat packet will cause Android to think that no heartbeat packet has been received for 5 seconds and offline this device
        doSetStepVolume(root);
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_SET_EXIT:
        return false;
        break;
    case LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_ALIVE:
        doSetAlive();
        break;
    default:
        return false;
    }
    return true;
}

// Function to check if the cast server is alive
bool LightningCastClient::checkCastServerAlive()
{
    struct timespec now = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    long check_diff_time_ms = diff_in_us(aliveTime, now)/1000;
    if(check_diff_time_ms > 5000) {
        return false;
    }
    return true;
}

// Function to set LightningCast version
void LightningCastClient::doSetLightningcastVersion(const Json::Value &root)
{
    int major = root["Major"].asInt();
    int minor = root["Minor"].asInt();
    int patch = root["Patch"].asInt();
    DEBUG("doSetLightningcastVersion major=%d, minor=%d, patch=%d\n", major, minor, patch);
}

// Function to set metadata
void LightningCastClient::doSetMetadata(const Json::Value &root)
{
    std::string title = root["Title"].asString();
    std::string album = root["Album"].asString();
    std::string artist = root["Artist"].asString();
    std::string cover_url = root["CoverUrl"].asString();
    size_t pos = cover_url.find("http://localhost");
    if (pos != std::string::npos) {
        // Replace "localhost" with android address
        cover_url.replace(pos+strlen("http://"), std::string("localhost").length(), serverIP);
    }
    DEBUG("doSetMetadata title=%s, album=%s, artist=%s, cover_url=%s\n", title.c_str(), album.c_str(), artist.c_str(), cover_url.c_str());
    if(title == this->title && album == this->album && artist == this->artist && cover_url == this->cover_url)
    {
        return;
    }
    else
    {
        this->title = title;
        this->album = album;
        this->artist = artist;
        this->cover_url = cover_url;
    }
    printf("get Metadata title=%s, album=%s, artist=%s, cover_url=%s\n", title.c_str(), album.c_str(), artist.c_str(), cover_url.c_str());
}

// Function to set audio format
void LightningCastClient::doSetAudioFormat(const Json::Value &root)
{
    sample_rate = root["SampleRate"].asInt();
    format = root["Format"].asInt();
    channels = root["Channels"].asInt();
    DEBUG("doSetAudioFormat sample_rate=%d, format=%d, channels=%d\n", sample_rate, format, channels);
    printf("get AudioFormat sample_rate=%d, format=%d, channels=%d\n", sample_rate, format, channels);
}

// Function to set duration
void LightningCastClient::doSetDuration(const Json::Value &root)
{
    int duration = root["Duration"].asInt() / 1000;
    DEBUG("doSetDuration duration=%d\n", duration);
    if(duration == this->duration)
    {
        return;
    }
    else
    {
        this->duration = duration;
    }
    printf("get duration=%d\n", duration);
}

// Function to set position
void LightningCastClient::doSetPosition(const Json::Value &root)
{
    position = root["Position"].asInt();
    // DEBUG("doSetPosition position=%d\n", position);
}

// Function to set status
void LightningCastClient::doSetStatus(const Json::Value &root)
{
    play_status = root["PlayStatus"].asInt();
    DEBUG("doSetStatus play_status=%d\n", play_status);
}

// Function to play
void LightningCastClient::doPlay(const Json::Value &root)
{
    snapcastComm.Open();
    if(!snapcastComm.sendStart(serverIP, sample_rate, format, channels))
    {
        snapcastComm.Close();
    }
}

// Function to pause
void LightningCastClient::doPause()
{
    snapcastComm.sendPause();
    snapcastComm.Close();
}

// Function to stop
void LightningCastClient::doStop()
{
    snapcastComm.sendStop();
    snapcastComm.Close();
}

// Function to set volume
void LightningCastClient::doSetVolume(const Json::Value &root)
{
    current_volume = root["SET_VOLUME"].asInt();
    DEBUG("doSetVolume volume=%d\n", current_volume);
    printf("get volume=%d\n", current_volume);
}

// Function to set mute
void LightningCastClient::doSetMute(const Json::Value &root)
{
    int mute = root["SET_MUTE"].asInt();
    DEBUG("doSetMute volume=%d\n", mute);
}

// Function to set step volume
void LightningCastClient::doSetStepVolume(const Json::Value &root)
{
    int step_volume = root["SET_STEP_VOLUME"].asInt();
    DEBUG("doSetStepVolume step_volume=%d\n", step_volume);
}

// Function to set alive
void LightningCastClient::doSetAlive()
{
    clock_gettime(CLOCK_MONOTONIC, &aliveTime);
}

// First send the verification code and size, then send the actual data
void LightningCastClient::processSend(const string &str)
{
    std::lock_guard<std::mutex> lock(server_mutex);
    if(server_fd == -1)
    {
        return;
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
}

// Function to send remote command
void LightningCastClient::sendRemoteCommand(const LightningCastCommCmd &cmd, const int value)
{
    Json::Value rootWriter;
    Json::FastWriter send_writer;
    rootWriter["Cmd"] = (int)cmd;
    if(cmd == LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VERSION) {
        rootWriter["Major"] = lightningcast_version.major;
        rootWriter["Minor"] = lightningcast_version.minor;
        rootWriter["Patch"] = lightningcast_version.patch;
    } else {
        rootWriter["Value"] = value;
    }
    const std::string send_string = send_writer.write(rootWriter);
    processSend(send_string);
}

// Function to send alive signal
void LightningCastClient::sendAlive()
{
    Json::Value rootWriter;
    Json::FastWriter send_writer;
    rootWriter["Cmd"] = (int)LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_ALIVE;
    const std::string send_string = send_writer.write(rootWriter);
    processSend(send_string);
}
