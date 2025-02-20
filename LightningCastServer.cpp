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
#include "LightningCastServer.h"
using namespace std;

// Function to create and bind a socket
static int create_and_bind(const char *host, const int portNum, int *sockfd)
{
    int   listenfd;
    const int reuse = 1;
    int ret;

    if (portNum < 1024 || portNum > 32767)
        return -1;

    listenfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (listenfd < 0)
    {
        return -1;
    }
    ret = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,
			 (const char *) &reuse, sizeof(reuse));
	if (ret < 0) {
		close(listenfd);
		return -1;
	}

    struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));
	sin.sin_port = htons(portNum);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = (host == NULL) ? INADDR_ANY : inet_addr(host);
	
	ret = bind(listenfd, (struct sockaddr *)&sin, sizeof(sin));
	if (ret < 0) {
		close(listenfd);
		return -1;
	}

	ret = listen(listenfd, 5);
	if (ret < 0) {
		close(listenfd);
		return -1;
	}
	make_socket_non_blocking(listenfd);

    *sockfd = listenfd;
    return 0;
}

// Method to handle server operations
void LightningCastServer::handle()
{
    int listener;
    fd_set read_fds,master;
    int fdmax;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    int rc = create_and_bind(NULL, LIGHTNINGCASTTCPPORT, &listener);
    if(rc == -1)
    {
        printf("create_and_bind port=%d failed.\n", LIGHTNINGCASTTCPPORT);
        exit(-1);
    }
    FD_SET(listener, &master);
    FD_SET(wake_fd.Get(), &master);
    fdmax = listener > wake_fd.Get() ? listener : wake_fd.Get();
    bool stop = false;
    while(!stop)
    {
        read_fds = master;
        if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("PlayerClient select() error!");
            exit(1);
        }
        for(int i = 0; i <= fdmax; i++)
        {
            if(FD_ISSET(i, &read_fds))
            {
                if(i == listener)
                {
                    struct sockaddr_in client_addr;
                    memset(&client_addr, 0, sizeof(client_addr));
                    socklen_t client_len = sizeof(client_addr);
                    int fd;
                    if((fd = accept(listener, (struct sockaddr *)&client_addr, &client_len)) == -1)
                    {
                        perror("LightningCastServer accept() error!");
                    }
                    else
                    {
                        make_socket_non_blocking(fd);
                        FD_SET(fd, &master);
                        if(fd > fdmax)
                        {
                            fdmax = fd;
                        }
                        char client_ipv4_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &client_addr.sin_addr, client_ipv4_str, INET_ADDRSTRLEN);
                    }
                }
                else if (i == wake_fd.Get())
                {
                    wake_fd.Read();
                    stop = true;
                    break;
                }
                else
                {
                    if(!doRead(i))
                    {
                        shutdown(i, SHUT_RDWR);
                        close(i);
                        FD_CLR(i, &master);
                    }
                }
            }
        }
    }

    close(listener);
}

// Method to read data from client
bool LightningCastServer::doRead(const int client_fd)
{
    ssize_t result;
    lightningCast_header hi;

    // First receive the header
    hi.clear();
    uint8_t *hi_buf = (uint8_t *)&hi;
    size_t hi_length = 0;
    // Receive actual data until sizeof(lightningCast_header) is reached
    while (hi_length < sizeof(lightningCast_header))
    {
        result = read(client_fd, hi_buf + hi_length, sizeof(lightningCast_header) - hi_length);
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
        result = read(client_fd, buf + length, hi.data_size - length);
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
    return doParse(client_fd, buf, length);
}

// First send the verification code and size, then send the actual data
void LightningCastServer::processSend(const int client_fd, const std::string &str)
{
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
        written_bytes = ::write(client_fd, ptr, bytes_left);
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

// Method to open a connection
void LightningCastServer::Open(const int client_fd, const std::string &ip_addr)
{
    bool is_open = lightningCastClient.Open(ip_addr);
    Json::Value rootWriter;
    Json::FastWriter send_writer;
    rootWriter["Cmd"] = (int)LightningCastHandshakeCmd::LightningCastHandshakeCmd_OPEN;
    rootWriter["IsOpen"] = is_open ? 1 : 0;
    const std::string send_string = send_writer.write(rootWriter);
    processSend(client_fd, send_string);
}

// Method to parse data from client
bool LightningCastServer::doParse(const int client_fd, const uint8_t *buf, const size_t length)
{
    Json::Reader reader;
    Json::Value root;
    std::string data((const char *)buf, length);
    if (!reader.parse(data, root, false))
    {
        return false;
    }
    std::string ip_addr;
    LightningCastHandshakeCmd recv_cmd = (LightningCastHandshakeCmd)root["Cmd"].asInt();
    switch (recv_cmd)
    {
    case LightningCastHandshakeCmd::LightningCastHandshakeCmd_OPEN:
        ip_addr = root["IpAddr"].asString();
        Open(client_fd, ip_addr);
        break;
    default:
        return false;
    }
    return true;
}

// Method to exit the server
void LightningCastServer::Exit()
{
    wake_fd.Write();
}

// Method to handle service publishing
void ServicePublisher::handle()
{
    std::string service_name = "LightningCastDeviceSample";
    mdns_tinysvcmdns_register(service_name.c_str(), nullptr, LIGHTNINGCASTTCPPORT, nullptr, nullptr);
    while(!stop_thread)
    {
        usleep(500*1000);
    }
}

// Method to register mDNS service
int ServicePublisher::mdns_tinysvcmdns_register(
    const char *ap1name, __attribute__((unused)) char *ap2name, int port,
    __attribute__((unused)) char **txt_records,
    __attribute__((unused)) char **secondary_txt_records) {
    struct ifaddrs *ifalist;
    struct ifaddrs *ifa;

    svr = mdnsd_start();
    if (svr == NULL) {
        printf("tinysvcmdns: mdnsd_start() failed\n");
        return -1;
    }

    // Thanks to Paul Lietar for this
    // room for name + .local + NULL
    char hostname[100 + 6];
    strncpy(hostname, "LightningCastDeviceSample", 99);
    // gethostname(hostname, 99);
    // according to POSIX, this may be truncated without a final NULL !
    hostname[99] = 0;

    // will not work if the hostname doesn't end in .local
    char *hostend = hostname + strlen(hostname);
    if ((strlen(hostname) < strlen(".local")) ||
        (strcmp(hostend - 6, ".local") != 0)) {
        strcat(hostname, ".local");
    }

    if (getifaddrs(&ifalist) < 0) {
        printf("tinysvcmdns: getifaddrs() failed\n");
        return -1;
    }

    ifa = ifalist;

    // Look for an ipv4/ipv6 non-loopback interface to use as the main one.
    for (ifa = ifalist; ifa != NULL; ifa = ifa->ifa_next) {
        // only check for the named interface, if specified

        if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr &&
            ifa->ifa_addr->sa_family == AF_INET) {
            uint32_t main_ip =
                ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;

            mdnsd_set_hostname(svr, hostname,
                               main_ip);  // TTL should be 120 seconds
            break;
        }
    }

    if (ifa == NULL) {
        printf("tinysvcmdns: no non-loopback ipv4 or ipv6 interface found\n");
        return -1;
    }

    // Skip the first one, it was already added by set_hostname
    for (ifa = ifa->ifa_next; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_flags & IFF_LOOPBACK)  // Skip loop-back interfaces
            continue;
        // only check for the named interface, if specified

        switch (ifa->ifa_addr->sa_family) {
            case AF_INET: {  // ipv4
                uint32_t ip =
                    ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
                struct rr_entry *a_e =
                    rr_create_a(create_nlabel(hostname),
                                ip);  // TTL should be 120 seconds
                mdnsd_add_rr(svr, a_e);
            } break;
        }
    }

    freeifaddrs(ifa);

    char txt_ver[] = "ver=1.0";
    char *txtwithoutmetadata[] = {txt_ver, NULL};

    char **txt;

    txt = txtwithoutmetadata;

    char regtype[] = "_lightningcast._tcp";

    char *extendedregtype =
        (char *)malloc(strlen(regtype) + strlen(".local") + 1);

    if (extendedregtype == NULL)
        printf(
            "tinysvcmdns: could not allocated memory to request a Zeroconf "
            "service\n");

    strcpy(extendedregtype, regtype);
    strcat(extendedregtype, ".local");

    struct mdns_service *svc = mdnsd_register_svc(
        svr, ap1name, extendedregtype, port, NULL,
        (const char **)txt);  // TTL should be 75 minutes, i.e. 4500 seconds
    mdns_service_destroy(svc);

    free(extendedregtype);

    return 0;
}

// Method to unregister mDNS service
void ServicePublisher::mdns_tinysvcmdns_unregister(void) {
    if (svr) {
        mdnsd_stop(svr);
        svr = NULL;
    }
}
