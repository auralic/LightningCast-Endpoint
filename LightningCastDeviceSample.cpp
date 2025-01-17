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
#include <list>
#include <map>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <poll.h>
#include "Util.h"
#include "LightningCastServer.h"
#include "LightningCastClient.h"
#include "SnapcastComm.h"

// Function to initialize signal handlers
void init_sig(int sig)
{
    struct sigaction action;

    switch (sig) {
        case SIGPIPE:
            action.sa_handler = [](int) {
                std::cerr << "Received SIGPIPE, ignoring..." << std::endl;
            };
            break;
        case SIGTERM:
        case SIGINT:
            action.sa_handler = [](int) {
                std::cerr << "Press q to shutting down..." << std::endl;
            };
            break;
        default:
            return;
    }

    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(sig, &action, nullptr);
}

int main(int argc, char *argv[])
{
    init_sig(SIGPIPE);
    init_sig(SIGTERM);
    init_sig(SIGINT);

    std::cout << "Press k and enter to play" << std::endl;
    std::cout << "Press s and enter to stop" << std::endl;
    std::cout << "Press p and enter to pause" << std::endl;
    std::cout << "Press n and enter to next" << std::endl;
    std::cout << "Press b and enter to previous" << std::endl;
    std::cout << "Press r and enter to increase volume" << std::endl;
    std::cout << "Press e and enter to decrease volume" << std::endl;
    std::cout << "Press u and enter to fast forward" << std::endl;
    std::cout << "Press y and enter to rewind" << std::endl;
    std::cout << "Press q and enter to exit" << std::endl;

    ServicePublisher servicePublisher;
    SnapcastComm snapcastComm;
    LightningCastClient lightningCastClient(snapcastComm);
    LightningCastServer lightningCastServer(lightningCastClient);

    std::thread servicePublisherThread = std::thread([&] { servicePublisher.handle(); });
    std::thread snapcastCommThread = std::thread([&] { snapcastComm.handle(); });
    std::thread lightningCastClientThread = std::thread([&] { lightningCastClient.handle(); });
    std::thread lightningCastServerThread = std::thread([&] { lightningCastServer.handle(); });

    while (true) {
        struct pollfd fds[1];
        fds[0].fd = STDIN_FILENO; // Monitor standard input
        fds[0].events = POLLIN;   // Check if there is data to read

        int ret = poll(fds, 1, 100); // Wait for 100 milliseconds

        if (ret > 0) {
            if (fds[0].revents & POLLIN) { // Check if there is input
                char ch;
                read(STDIN_FILENO, &ch, 1); // Read one character

                if (ch == 'q') { // Press 'q' to exit
                    std::cout << "Exiting...\n";
                    break;
                }

                /*
                Key operation instructions:
                k - Play
                s - Stop
                p - Pause
                n - Next
                b - Previous
                r - Increase volume
                e - Decrease volume
                u - Fast forward
                y - Rewind
                */
                switch (ch) {
                    case 'k':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_PLAY);
                        break;
                    case 's':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_STOP);
                        break;
                    case 'p':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_PAUSE);
                        break;
                    case 'n':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_NEXT);
                        break;
                    case 'b':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_PREVIOUS);
                        break;
                    case 'r':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VOLUME, (lightningCastClient.current_volume+5) < 100 ? lightningCastClient.current_volume+5 : 100);
                        break;
                    case 'e':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_VOLUME, (lightningCastClient.current_volume-5) > 0 ? lightningCastClient.current_volume-5 : 0);
                        break;
                    case 'u':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_SEEK, lightningCastClient.position+5);
                        break;
                    case 'y':
                        lightningCastClient.sendRemoteCommand(LightningCastCommCmd::LightningCastCommCmdCmd_NOTIFY_SEEK, lightningCastClient.position-5);
                        break;
                    default:
                        break;
                }
            }
        }
    }
    lightningCastClient.Exit();
    snapcastComm.Exit();
    lightningCastServer.Exit();

    servicePublisher.mdns_tinysvcmdns_unregister();
    servicePublisher.stop_thread = true;
    if (servicePublisherThread.joinable()) {
        servicePublisherThread.join();
    }
    if (snapcastCommThread.joinable()) {
        snapcastCommThread.join();
    }
    if (lightningCastClientThread.joinable()) {
        lightningCastClientThread.join();
    }
    if (lightningCastServerThread.joinable()) {
        lightningCastServerThread.join();
    }

    return 0;
}
