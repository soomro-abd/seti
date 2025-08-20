#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>

#define PORT 12345

int main()
{
    while (1)
    {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        char buffer[1024] = {0};
        struct timespec os_time;

        printf("Creating socket...\n");
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }
        printf("Socket created successfully.\n");

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(PORT);

        printf("Binding socket...\n");
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        printf("Socket bound successfully.\n");

        printf("Listening on socket...\n");
        if (listen(server_fd, 3) < 0)
        {
            perror("listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        printf("Socket is now listening.\n");

        while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0)
        {
            printf("Connection accepted.\n");
            read(new_socket, buffer, 1024);
            printf("Received message: %s\n", buffer);

            if (strncmp(buffer, "time", 4) == 0)
            {
                clock_gettime(CLOCK_REALTIME, &os_time);
                printf("Sending current time: %ld.%ld\n", os_time.tv_sec, os_time.tv_nsec);
                send(new_socket, &os_time, sizeof(os_time), 0);
            }
            close(new_socket);
            printf("Connection closed.\n");
        }
        if (new_socket < 0)
        {
            perror("accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
    }
}