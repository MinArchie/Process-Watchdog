#include <stdio.h>
#include <unistd.h>

int main() {
    while (1) {
        printf("Hello, World!\n");
        sleep(2);  // Wait for 2 seconds
    }
    return 0;
}

