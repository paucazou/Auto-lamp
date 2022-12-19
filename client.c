#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define ADDRESS "192.168.1.22"
#define PORT 3333

int DEBUG = 0;

void debug(char *format, ...) {
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
        printf("Usage: %s [0/1] [hh:mm] [hh:mm]\n", argv[0]);
        printf("\n");
        printf("  [0/1]      Select morning or evening period\n");
        printf("  [hh:mm]    Start of the period between 00:00 and 23:59\n");
        printf("  [hh:mm]    End of the period between 00:00 and 23:59\n");
        return 0;
    }

    if (argc != 4) {
        printf("Error: script requires 3 arguments\n");
        return 1;
    }

    char *arg1 = argv[1];
    char *arg2 = argv[2];
    char *arg3 = argv[3];

    if (strcmp(arg1, "0") != 0 && strcmp(arg1, "1") != 0) {
        printf("Error: first argument must be 0 or 1\n");
        return 1;
    }

    char msg[5];
    int pos=0;
    msg[pos++] = (char)atoi(arg1);

    char *hours[2] = {arg2, arg3};
    for (int i = 0; i < 2; i++) {
        char hour[3] = {0};
        char minute[3] = {0};
        sscanf(hours[i], "%2s:%2s", hour, minute);

        if (atoi(hour) < 0 || atoi(hour) > 23) {
            printf("Error: invalid hour: %s\n", hours[i]);
            return 1;
        }
        if (atoi(minute) < 0 || atoi(minute) > 59) {
            printf("Error: invalid minute: %s\n", hours[i]);
            return 1;
        }

        char ascii_hour = (char)atoi(hour);
        char ascii_minute = (char)atoi(minute);
        debug("hour %d",ascii_hour);
        debug("minute %d", ascii_minute);
        msg[pos++] = ascii_hour;
        msg[pos++] = ascii_minute;
    }


    int try = 1;
    while (try != 0) {
        printf("Try: %d...\n", try);

        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1) {
            perror("Error creating socket");
            return 1;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PORT);
        inet_aton(ADDRESS, &addr.sin_addr);

        if (sendto(sock, msg, 5, 0, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("Error sending message");
            close(sock);
            return 1;
        }

        char res[256] = {0};
        socklen_t addr_len = sizeof(addr);
        if (recvfrom(sock, res, sizeof(res), 0, (struct sockaddr *)&addr, &addr_len) == -1) {
            perror("Error receiving response");
            close(sock);
            return 1;
        }

        if (strstr(res, "invalid") != NULL) {
            try = 0;
            printf("Invalid message. Please check\n");
        } else if (strcmp(res, msg) == 0) {
            try = 0;
        } else {
            try++;
        }

        close(sock);
    }

    return 0;
}

