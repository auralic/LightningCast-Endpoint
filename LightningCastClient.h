#ifndef _LIGHTNINGCASTCLIENT_H
#define _LIGHTNINGCASTCLIENT_H

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
#include <mutex>
#include <condition_variable>
#include "SnapcastComm.h"

// Class for Lightningcast version
class LightningcastVersion {
public:
    int major;
    int minor;
    int patch;
    LightningcastVersion(int _major, int _minor, int _patch) : major(_major), minor(_minor), patch(_patch) {}
    bool operator<(const LightningcastVersion &other) const {
        return std::tie(major, minor, patch) < std::tie(other.major, other.minor, other.patch);
    }
};

// Enum for LightningCast client commands
enum class LightningCastClientCmd
{
    LightningCastServerCmd_NONE = 0,
    LightningCastServerCmd_OPEN,
    LightningCastServerCmd_CLOSE,
    LightningCastServerCmd_EXIT
};

// This enum must be consistent with the tablet
enum class LightningCastCommCmd
{
    LightningCastCommCmd_NONE = 0,
    LightningCastCommCmdCmd_SET_VERSION = 1,
    LightningCastCommCmdCmd_SET_EXIT,
    LightningCastCommCmdCmd_SET_METADATA,
    LightningCastCommCmdCmd_SET_AUDIOFORMAT,
    LightningCastCommCmdCmd_SET_DURATION,
    LightningCastCommCmdCmd_SET_POSITION,
    LightningCastCommCmdCmd_SET_STATUS,
    LightningCastCommCmdCmd_SET_PLAY,
    LightningCastCommCmdCmd_SET_PAUSE,
    LightningCastCommCmdCmd_SET_STOP,
    LightningCastCommCmdCmd_SET_VOLUME,
    LightningCastCommCmdCmd_SET_MUTE,
    LightningCastCommCmdCmd_SET_STEP_VOLUME,
    LightningCastCommCmdCmd_NOTIFY_VERSION = 101,
    LightningCastCommCmdCmd_NOTIFY_EXIT,
    LightningCastCommCmdCmd_NOTIFY_ALIVE,
    LightningCastCommCmdCmd_NOTIFY_PLAY,
    LightningCastCommCmdCmd_NOTIFY_PAUSE,
    LightningCastCommCmdCmd_NOTIFY_STOP,
    LightningCastCommCmdCmd_NOTIFY_PREVIOUS,
    LightningCastCommCmdCmd_NOTIFY_NEXT,
    LightningCastCommCmdCmd_NOTIFY_SEEK,
    LightningCastCommCmdCmd_NOTIFY_VOLUME_CONTROL,
    LightningCastCommCmdCmd_NOTIFY_VOLUME,
    LightningCastCommCmdCmd_NOTIFY_MUTE
};

// Class for metadata
class Metadata {
public:
    std::string title;
    std::string album;
    std::string artist;
    std::string cover_url;
    Metadata() {}
    Metadata(const std::string &_title, const std::string &_album,
             const std::string &_artist, const std::string &_cover_url)
        : title(_title),
          album(_album),
          artist(_artist),
          cover_url(_cover_url) {}
    Metadata &operator=(const Metadata &MetadataOther) {
        title = MetadataOther.title;
        album = MetadataOther.album;
        artist = MetadataOther.artist;
        cover_url = MetadataOther.cover_url;
        return *this;
    }
};

// Class for audio data format
class AudioDataFormat {
public:
    int sample_rate;
    int format;
    int channels;
    AudioDataFormat() : sample_rate(0), format(0), channels(0) {}
    AudioDataFormat(const int sample_rate, const int format, const int channels)
        : sample_rate(sample_rate), format(format), channels(channels) {}
    AudioDataFormat &operator=(const AudioDataFormat &AudioFormatOther) {
        sample_rate = AudioFormatOther.sample_rate;
        format = AudioFormatOther.format;
        channels = AudioFormatOther.channels;
        return *this;
    }
};

// Class for music information
class MusicInfo {
public:
    Metadata metadata;
    AudioDataFormat audio_format;
    int duration;
    int position;
    int play_status;
    MusicInfo() : duration(0), position(0), play_status(0) {}
};

// Class for LightningCast client
class LightningCastClient
{
public:
    SnapcastComm &snapcastComm;
    LightningcastVersion lightningcast_version;
    bool running;
    LightningCastClientCmd cmd;
    MusicInfo musicInfo;
    std::mutex mutex;
    std::condition_variable cond;
    std::condition_variable cond_sync;
    bool ready;
    bool ready_sync;
    bool is_open;
    std::mutex server_mutex;
    int server_fd;
    std::string serverIP;
    EventFD wake_fd;
    int sample_rate;
    int format;
    int channels;
    std::string metadata_json;
    int duration;
    int position;
    int play_status;
    int current_volume;
    struct timespec aliveTime;
    std::string title;
    std::string album;
    std::string artist;
    std::string cover_url;

    LightningCastClient(SnapcastComm &_snapcastThread):snapcastComm(_snapcastThread),lightningcast_version(1,0,0),running(true),
                        cmd(LightningCastClientCmd::LightningCastServerCmd_NONE),ready(false),ready_sync(false),is_open(false),server_fd(-1),
                        sample_rate(0),format(0),channels(0),duration(0),position(0),play_status(0), current_volume(50), aliveTime({0,0})
    {
    }
    ~LightningCastClient()
    {
    }

    bool getIsOpen()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return is_open;
    }
    void setIsOpen(bool _is_open)
    {
        std::lock_guard<std::mutex> lock(mutex);
        is_open = _is_open;
    }

    void handle();
    void do_open();
    void finishCmd();
    void LockfinishCmd();
    bool Open(std::string _serverIP);
    void Close();
    bool connectServer();
    void Exit();

    bool doRead(int fd);
    bool doParse(const int fd, const uint8_t *buf, size_t length);
    void doSetLightningcastVersion(const Json::Value &root);
    void doSetMetadata(const Json::Value &root);
    void doSetAudioFormat(const Json::Value &root);
    void doSetDuration(const Json::Value &root);
    void doSetPosition(const Json::Value &root);
    void doSetStatus(const Json::Value &root);
    void doPlay(const Json::Value &root);
    void doPause();
    void doStop();
    void doSetVolume(const Json::Value &root);
    void doSetMute(const Json::Value &root);
    void doSetStepVolume(const Json::Value &root);
    void doSetAlive();
    void processSend(const std::string &str);
    void sendRemoteCommand(const LightningCastCommCmd &cmd, const int value=0);
    void sendAlive();
    bool checkCastServerAlive();
};

#endif
