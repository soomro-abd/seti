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

#define PORT 18845                // Port for network communication
#define IP_ADDRESS "172.24.41.23" // IP address of the network time server

// Shared data structure for TSC and time data
typedef struct
{
    uint64_t tsc;
    struct timespec os_time;
} TSCData;

// Global Vars
uint64_t attack_increment = 0; // Drift attack increment
atomic_long counter = 0;
bool adder_running = true;  // Flag to control the adder thread's execution
double final_average = 0.0; // Global variable to store the final average
uint64_t typical_tsc_diff = 0;
atomic_bool large_gap_detected = false;
TSCData shared_tsc_data;
pthread_mutex_t shared_data_mutex = PTHREAD_MUTEX_INITIALIZER;
atomic_long delta_t = 0; // Time difference between local and network time

// Timers
struct timespec app_start_time;

static inline uint64_t rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc_value = ((uint64_t)hi << 32) | lo;
    return tsc_value;
}

static inline uint64_t compromised_rdtsc()
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc_value = ((uint64_t)hi << 32) | lo;
    tsc_value += attack_increment;
    attack_increment += 100000;
    return tsc_value;
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

void save_time_error_to_file(long time_error)
{
    FILE *file = fopen("time_error.txt", "a");
    if (file == NULL)
    {
        perror("Error opening file to save time error");
        return;
    }
    fprintf(file, "%ld\n", time_error);
    fclose(file);
}

// Function to save delta_t values to a file
void save_delta_to_file(long delta)
{
    FILE *file = fopen("delta_times.txt", "a");
    if (file == NULL)
    {
        perror("Error opening file to save delta_t");
        return;
    }
    // fprintf(file, "%ld\n", delta);
    fprintf(file, "%ld\n", delta);
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
    printf("Avg number of adds executed: %f\n", final_average);

    // Calculate the typical TSC difference
    typical_tsc_diff = tsc_diff / 1000;
    printf("Typical TSC difference in 2ms: %lu\n", typical_tsc_diff);

    // Signal the adder thread to stop
    adder_running = false;

    return NULL;
}

void *tsc_monitor_thread(void *arg)
{
    const int warmup_iterations = 10000;
    uint64_t total_difference = 0;
    uint64_t current_tsc, last_tsc = compromised_rdtsc();
    struct timespec req = {0, 10000}; // 10 microseconds = 10000 nanoseconds

    // Warm-up phase to calculate the typical TSC difference
    for (int i = 0; i < warmup_iterations; ++i)
    {
        nanosleep(&req, NULL); // Short sleep to space out TSC readings
        current_tsc = compromised_rdtsc();
        uint64_t difference = current_tsc - last_tsc;
        total_difference += difference;
        last_tsc = current_tsc;
    }

    // Calculate the typical difference
    uint64_t typical_difference = total_difference / warmup_iterations;
    printf("Typical TSC difference between consecutive reads: %lu\n", typical_difference);

    // Start monitoring for large gaps
    while (1)
    {
        if (!atomic_load(&large_gap_detected))
        {

            nanosleep(&req, NULL); // Short sleep to space out TSC readings
            current_tsc = compromised_rdtsc();

            uint64_t difference = current_tsc - last_tsc;

            // Check if the difference exceeds 10 times the typical difference
            if (last_tsc > 0 && (difference > 10 * typical_difference))
            {
                pthread_mutex_lock(&shared_data_mutex);
                shared_tsc_data.tsc = current_tsc;
                clock_gettime(CLOCK_REALTIME, &shared_tsc_data.os_time);
                large_gap_detected = true; // Signal large gap detection
                pthread_mutex_unlock(&shared_data_mutex);
            }
            last_tsc = current_tsc;
        }
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

            struct timespec received_time;
            request_network_time(&received_time);

            // Convert attack increment to nanoseconds
            uint64_t attack_increment_ns = attack_increment / 3;

            // Save time error to file
            save_time_error_to_file(attack_increment_ns);

            // Reset attack increment
            attack_increment = 0;

            struct timespec os_time;
            clock_gettime(CLOCK_REALTIME, &os_time);

            // Convert current time to nanoseconds
            uint64_t current_time_ns = os_time.tv_sec * 1000000000 + os_time.tv_nsec + attack_increment_ns;

            // Convert received time to nanoseconds
            uint64_t received_time_ns = received_time.tv_sec * 1000000000 + received_time.tv_nsec;

            // Calculate the difference between the two times
            delta_t = received_time_ns - current_time_ns;

            // Convert delta_t to milliseconds
            delta_t /= 1000000;

            printf("Delta_t calculated (ms): %ld\n", delta_t);

            // Save delta_t to file each time it is updated
            save_delta_to_file(delta_t);
        }
    }
    return NULL;
}

void *calibrate_thread(void *arg)
{
    while (1)
    {
        // Calibration loop: execute X instructions and validate TSC diff
        uint64_t start_tsc = compromised_rdtsc();
        for (long i = 0; i < final_average; ++i)
        {
            __asm__ __volatile__("add $1, %0" : "+r"(i));
        }
        uint64_t end_tsc = compromised_rdtsc();

        // Check if TSC difference falls within the acceptable range
        if (end_tsc - start_tsc > typical_tsc_diff * 1.01)
        {
            fprintf(stderr, "Error: TSC difference out of bounds.\n");
            save_exit_duration();
            exit(EXIT_FAILURE);
        }
    }
}


int main()
{
    printf("Starting application...\n");

    clock_gettime(CLOCK_REALTIME, &app_start_time); // Record the application start time

    pthread_t adder_thread, timer_thread;

    // Create threads
    pthread_create(&adder_thread, NULL, adder, NULL);
    pthread_create(&timer_thread, NULL, timer, NULL);

    // Wait for the timer thread to finish
    pthread_join(timer_thread, NULL);

    // Wait for the adder thread to finish after stopping it
    pthread_join(adder_thread, NULL);

    pthread_t tsc_thread, sync_thread, calib_thread;

    printf("Sleeping for 5 seconds...\n");
    sleep(5);

    pthread_create(&tsc_thread, NULL, tsc_monitor_thread, NULL);
    pthread_create(&sync_thread, NULL, sync_and_calibrate_thread, NULL);
    pthread_create(&calib_thread, NULL, calibrate_thread, NULL);

    pthread_join(tsc_thread, NULL);
    pthread_join(sync_thread, NULL);
    pthread_join(calib_thread, NULL);

    save_exit_duration(); // Record the application exit duration if it exits normally
    return 0;
}