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

#define TSC_THRESHOLD 100         // Arbitrary threshold for TSC difference check
#define PORT 18845                // Port for network communication
#define IP_ADDRESS "172.24.41.23" // IP address of the network time server

// Shared counter (atomic to avoid race conditions)
atomic_long counter = 0;
double final_average = 0.0; // Global variable to store the final average
uint64_t typical_tsc_diff = 0;
bool adder_running = true;  // Flag to control the adder thread's execution

// Shared variables
atomic_bool large_gap_detected = false;
atomic_long tsc_counter = 0;
atomic_long last_tsc_read = 0;
atomic_long delta_t = 0;

// Shared data structure for TSC and time data
typedef struct
{
    uint64_t tsc;
    struct timespec os_time;
} TSCData;

TSCData shared_tsc_data;
pthread_mutex_t shared_data_mutex = PTHREAD_MUTEX_INITIALIZER;

// Track application start time for measuring exit duration
struct timespec app_start_time;

uint64_t attack_increment = 0; // Drift attack increment

uint64_t attack_increment_two = 0; // Time Error attack increment

static inline uint64_t rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc_value = ((uint64_t)hi << 32) | lo;
    tsc_value += attack_increment_two;
    attack_increment_two += 100000;
    return tsc_value;
}

// Function to save delta_t values to a file
void save_delta_to_file(long delta, struct timespec *current_time)
{
    FILE *file = fopen("delta_times.txt", "a");
    if (file == NULL)
    {
        perror("Error opening file to save delta_t");
        return;
    }
    // fprintf(file, "%ld\n", delta);
    fprintf(file, "%ld.%ld, %ld\n", current_time->tv_sec, current_time->tv_nsec, delta);
    fclose(file);
}

void save_time_error_to_file(long time_error)
{
    time_error /= 3000000;
    FILE *file = fopen("time_error.txt", "a");
    if (file == NULL)
    {
        perror("Error opening file to save time error");
        return;
    }
    fprintf(file, "%ld\n",  time_error);
    fclose(file);
}

// Function to save application exit duration to a file
void save_exit_duration()
{
    struct timespec end_time;
    clock_gettime(CLOCK_REALTIME, &end_time);
    long duration = (end_time.tv_sec - app_start_time.tv_sec) * 1000 + (end_time.tv_nsec - app_start_time.tv_nsec) / 1000000;

    FILE *file = fopen("exit_duration.txt", "a");
    if (file == NULL)
    {
        perror("Error opening file to save exit duration");
        return;
    }
    fprintf(file, "Application exit duration: %ld ms\n", duration);
    fclose(file);
}

// Thread 1: Continuously increments the counter
void *adder(void *arg)
{
    while (adder_running)
    {                                  // Only run while the flag is true
        atomic_fetch_add(&counter, 1); // Increment the shared counter
    }
    return NULL;
}

// Thread 2: Sleeps for 2 milliseconds, records the count, and calculates the final average
void *timer(void *arg)
{
    struct timespec req = {0, 2000000L}; // 2 ms sleep time (2,000,000 nanoseconds)
    long total_increments = 0;
    uint64_t tsc_diff = 0;

    for (int i = 0; i < 1000; ++i)
    {
        uint64_t current_tsc = rdtsc();
        long before = atomic_load(&counter);      // Read the counter before sleep
        nanosleep(&req, (struct timespec *)NULL); // Sleep for 2 ms
        long after = atomic_load(&counter);       // Read the counter after sleep
        uint64_t tsc_after = rdtsc();

        // Calculate the difference
        long difference = after - before;
        uint64_t tsc_difference = tsc_after - current_tsc;

        // Update the cumulative total
        total_increments += difference;
        tsc_diff += tsc_difference;
    }

    // Calculate the final average after all measurements
    final_average = (double)total_increments / 1000.0;
    printf("Final average: %f\n", final_average);

    // Calculate the typical TSC difference
    typical_tsc_diff = tsc_diff / 1000;
    printf("Typical TSC difference: %lu\n", typical_tsc_diff);

    // Signal the adder thread to stop
    adder_running = false;
    printf("Adder thread signaled to stop.\n");

    return NULL;
}


void *tsc_monitor_thread(void *arg)
{
    const int warmup_iterations = 10000;
    uint64_t total_difference = 0;
    uint64_t current_tsc, last_tsc = rdtsc();
    struct timespec req = {0, 10000}; // 10 microseconds = 10000 nanoseconds
    uint64_t min_difference = UINT64_MAX;
    uint64_t max_difference = 0;

    // Warm-up phase to calculate the typical TSC difference
    for (int i = 0; i < warmup_iterations; ++i)
    {
        nanosleep(&req, NULL); // Short sleep to space out TSC readings
        current_tsc = rdtsc();
        uint64_t difference = current_tsc - last_tsc;
        total_difference += difference;
        if (difference < min_difference)
        {
            min_difference = difference;
        }
        if (difference > max_difference)
        {
            max_difference = difference;
        }
        last_tsc = current_tsc;
    }

    // Calculate the typical difference
    uint64_t typical_difference = total_difference / warmup_iterations;
    printf("Typical TSC difference calculated: %lu\n", typical_difference);
    printf("Minimum TSC difference: %lu\n", min_difference);
    printf("Maximum TSC difference: %lu\n", max_difference);

    // Start monitoring for large gaps
    while (1)
    {
        nanosleep(&req, NULL); // Short sleep to space out TSC readings
        current_tsc = rdtsc();

        uint64_t difference = current_tsc - last_tsc;
        // printf("TSC difference: %lu\n", difference);
        // printf("Last TSC: %lu, Current TSC: %lu\n", last_tsc, current_tsc);

        // Check if the difference exceeds 100 times the typical difference
        if (last_tsc > 0 && (difference) > 10 * typical_difference)
        {
            pthread_mutex_lock(&shared_data_mutex);
            shared_tsc_data.tsc = current_tsc;
            clock_gettime(CLOCK_REALTIME, &shared_tsc_data.os_time);
            large_gap_detected = true; // Signal large gap detection
            pthread_mutex_unlock(&shared_data_mutex);
            printf("Large TSC gap detected: %lu (Threshold: %lu)\n",
                   difference, 10 * typical_difference);
            printf("Last TSC: %lu, Current TSC: %lu\n", last_tsc, current_tsc);
        }
        last_tsc = current_tsc;
    }
    return NULL;
}

void request_network_time(struct timespec *received_time)
{
    int sock = 0;
    struct sockaddr_in serv_addr;
    char *message = "time";

    printf("Creating socket...\n");
    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("Socket creation error");
        return;
    }
    printf("Socket created successfully.\n");

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    // Convert IP address and set it in serv_addr
    printf("Converting IP address...\n");
    if (inet_pton(AF_INET, "172.24.41.23", &serv_addr.sin_addr) <= 0)
    {
        perror("Invalid address/ Address not supported");
        close(sock);
        return;
    }
    printf("IP address converted successfully.\n");

    // Connect to the server
    printf("Connecting to the server...\n");
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Connection Failed");
        close(sock);
        return;
    }
    printf("Connected to the server successfully.\n");

    // Send "time" message to request the timestamp
    printf("Sending time request...\n");
    send(sock, message, strlen(message), 0);

    // Read the response (the timestamp)
    printf("Waiting for timestamp...\n");
    if (read(sock, received_time, sizeof(struct timespec)) <= 0)
    {
        perror("Failed to receive timestamp");
    }
    else
    {
        printf("Timestamp received: %ld.%ld\n", received_time->tv_sec, received_time->tv_nsec);
    }

    // Close the socket
    printf("Closing socket...\n");
    close(sock);
    printf("Socket closed.\n");
}

void *sync_and_calibrate_thread(void *arg)
{
    while (1)
    {
        if (atomic_load(&large_gap_detected))
        {
            printf("Large gap detected. Starting synchronization and calibration.\n");

            // Clear the gap detected flag and request network time
            atomic_store(&large_gap_detected, false);

            // Save time error to file
            save_time_error_to_file(attack_increment_two);

            // Clear attack increment
            attack_increment_two = 0;

            struct timespec received_time;
            request_network_time(&received_time);

            struct timespec os_time;
            clock_gettime(CLOCK_REALTIME, &os_time);

            // Convert current time to nanoseconds
            uint64_t current_time_ns = os_time.tv_sec * 1000000000 + os_time.tv_nsec;

            // Convert received time to nanoseconds
            uint64_t received_time_ns = received_time.tv_sec * 1000000000 + received_time.tv_nsec;
            
            // Calculate the difference between the two times
            delta_t = received_time_ns - current_time_ns;

            // Convert delta_t to milliseconds
            delta_t /= 1000000;

            // Current Time
            printf("Current time: %ld.%ld\n", os_time.tv_sec, os_time.tv_nsec);

            printf("Delta_t calculated (ms): %ld\n", delta_t);

            // // Calculate corrected time
            // struct timespec corrected_time;
            // corrected_time.tv_sec = os_time.tv_sec;
            // corrected_time.tv_nsec = os_time.tv_nsec + delta_t;

            // Save delta_t to file each time it is updated
            save_delta_to_file(delta_t, &os_time);

            // Calibration loop: execute X instructions and validate TSC diff
            uint64_t start_tsc = rdtsc();
            for (long i = 0; i < final_average; ++i)
            {
                __asm__ __volatile__("add $1, %0" : "+r"(i));
            }
            uint64_t end_tsc = rdtsc();
            // uint64_t end_tsc = rdtsc() + attack_increment;
            // attack_increment += 100000;

            printf("Calibration loop TSC start: %lu, end: %lu\nDifference: %lu\n", start_tsc, end_tsc, end_tsc - start_tsc);
            printf("Threshold: %lu\n", (unsigned long)(typical_tsc_diff * 1.01));

            // Check if TSC difference falls within the acceptable range
            if (end_tsc - start_tsc > typical_tsc_diff * 1.01)
            {
                fprintf(stderr, "Error: TSC difference out of bounds.\n");
                save_exit_duration();
                exit(EXIT_FAILURE);
            }
            else
            {
                printf("TSC difference within acceptable range.\n");
            }
        }
    }
    return NULL;
}

void *network_listener_thread(void *arg)
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

    // int server_fd, new_socket;
    // struct sockaddr_in address;
    // int addrlen = sizeof(address);
    // char buffer[1024] = {0};
    // struct timespec os_time;

    // if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
    //     perror("socket failed");
    //     exit(EXIT_FAILURE);
    // }

    // address.sin_family = AF_INET;
    // address.sin_addr.s_addr = INADDR_ANY;
    // address.sin_port = htons(PORT);

    // bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    // listen(server_fd, 3);

    // while ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) >= 0) {
    //     read(new_socket, buffer, 1024);

    //     if (strncmp(buffer, "time", 4) == 0 && !atomic_load(&large_gap_detected)) {
    //         clock_gettime(CLOCK_REALTIME, &os_time);
    //         send(new_socket, &os_time, sizeof(os_time), 0);
    //     }
    //     close(new_socket);
    // }
    return NULL;
}

int main()
{
    printf("Starting application...\n");

    clock_gettime(CLOCK_REALTIME, &app_start_time); // Record the application start time

    pthread_t adder_thread, timer_thread;

    // Create threads
    printf("Creating adder thread...\n");
    pthread_create(&adder_thread, NULL, adder, NULL);
    printf("Creating timer thread...\n");
    pthread_create(&timer_thread, NULL, timer, NULL);

    // Wait for the timer thread to finish
    printf("Waiting for timer thread to finish...\n");
    pthread_join(timer_thread, NULL);
    printf("Timer thread finished.\n");

    // Wait for the adder thread to finish after stopping it
    printf("Waiting for adder thread to finish...\n");
    pthread_join(adder_thread, NULL);
    printf("Adder thread finished.\n");

    pthread_t tsc_thread, sync_thread, network_thread;

    printf("Creating network listener thread...\n");
    pthread_create(&network_thread, NULL, network_listener_thread, NULL);

    printf("Sleeping for 5 seconds...\n");
    sleep(5);

    printf("Creating TSC monitor thread...\n");
    pthread_create(&tsc_thread, NULL, tsc_monitor_thread, NULL);
    printf("Creating sync and calibrate thread...\n");
    pthread_create(&sync_thread, NULL, sync_and_calibrate_thread, NULL);

    printf("Waiting for TSC monitor thread to finish...\n");
    pthread_join(tsc_thread, NULL);
    printf("TSC monitor thread finished.\n");

    printf("Waiting for sync and calibrate thread to finish...\n");
    pthread_join(sync_thread, NULL);
    printf("Sync and calibrate thread finished.\n");

    printf("Waiting for network listener thread to finish...\n");
    pthread_join(network_thread, NULL);
    printf("Network listener thread finished.\n");

    save_exit_duration(); // Record the application exit duration if it exits normally
    printf("Application exiting...\n");
    return 0;
}
