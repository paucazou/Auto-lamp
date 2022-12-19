#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define ADDRESS "192.168.1.22"
#define PORT 3333

int DEBUG = 0;

void debug(const char *format, ...) {
    if (DEBUG == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

int main(int argc, char *argv[]) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        std::cout << "Usage: " << argv[0] << " [0/1] [hh:mm] [hh:mm]" << std::endl;
        std::cout << std::endl;
        std::cout << "  [0/1]      Select morning or evening period" << std::endl;
        std::cout << "  [hh:mm]    Start of the period between 00:00 and 23:59" << std::endl;
        std::cout << "  [hh:mm]    End of the period between 00:00 and 23:59" << std::endl;
        return 0;
    }

    if (argc != 4) {
        std::cout << "Error: script requires 3 arguments" << std::endl;
        return 1;
    }

    std::string arg1 = argv[1];
    std::string arg2 = argv[2];
    std::string arg3 = argv[3];

    if (arg1 != "0" && arg1 != "1") {
        std::cout << "Error: first argument must be 0 or 1" << std::endl;
        return 1;
    }

    std::string msg = "";
    msg += static_cast<char>(stoi(arg1));

    std::string hours[2] = {arg2, arg3};
    for (int i = 0; i < 2; i++) {
        std::string hour = "";
        std::string minute = "";
        sscanf(hours[i].c_str(), "%2s:%2s", &hour[0], &minute[0]);

        if (stoi(hour) < 0 || stoi(hour) > 23) {
            std::cout << "Error: invalid hour: " << hours[i] << std::endl;
            return 1;
        }
        if (stoi(minute) < 0 || stoi(minute) > 59) {
            std::cout << "Error: invalid minute: " << hours[i] << std::endl;
            return 1;
        }

        char ascii_hour = static_cast<char>(stoi(hour));
        char ascii_minute = static_cast<char>(stoi(minute));

        debug("length ascii: hour %ld minute %ld\n", hour.length(), minute.length());
        msg += ascii_hour;
        msg += ascii_minute;
    }

    std::cout << "Msg length: " << msg.length() << std::endl;

    int try = 1;
    while (try != 0) {
        std::cout << "Try: " << try << "..." << std::endl;

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1) {
            perror("Error creating socket");
            return 1;
        }

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_aton(ADDRESS, &addr.sin_addr);

        if (sendto(sock, msg.c_str(), msg.length(), 0, (sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("Error sending message");
            close(sock);
            return 1;
        }

        char res[256] = {0};
        socklen_t addr_len = sizeof(addr);
        if (recvfrom(sock, res, sizeof(res), 0, (sockaddr *)&addr, &addr_len) == -1) {
            perror("Error receiving response");
            close(sock);
            return 1;
        }

        std::string res_str = res;
        if (res_str.find("invalid") != std::string::npos) {
            try = 0;
            std::cout << "Invalid message. Please check" << std::endl;
        } else if (res_str == msg) {
            try = 0;
        } else {
            try++;
        }

        close(sock);
    }

    return 0;
}

