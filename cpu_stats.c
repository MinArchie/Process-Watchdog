#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

void log_cpu_usage() {
    // Simulates CPU usage logging by reading from /proc/stat
    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        perror("Failed to open /proc/stat");
        return;
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Display a simplified CPU usage line
        printf("CPU Stats: %s", buffer);
    }
    fclose(fp);
}

int main() {
    time_t start_time, current_time;
    time(&start_time);

    printf("Starting perpetual CPU monitoring...\n");
    
    while (1) {
        log_cpu_usage();
        sleep(5);  // Wait for 5 seconds before logging again

        // Check how long the program has been running (optional, for watchdog logging)
        time(&current_time);
        if (difftime(current_time, start_time) > 3600) {  // Example: log every hour
            printf("Program has been running for over an hour.\n");
            time(&start_time);
        }
    }

    return 0;
}

