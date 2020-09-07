#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TACH_FILE_NAME "/dev/gpiotach1.0"
#define SCREEN_FILE_NAME "/dev/klcd"

bool done = false;

#define IOCTL_CLEAR_DISPLAY '0'
#define IOCTL_PRINT_ON_FIRST_LINE '1'

#define MAX_BUF_LEN 50  /* This seemingly must match the driver. Don't change it */
struct ioctl_message{
    char kbuf[MAX_BUF_LEN];
    unsigned int lineNumber; /* 1 or 2 */
    unsigned int nthCharacter; /* Where to start */
};

void trapper(int signum) {
    printf("Exiting from a signal trap: %d\n", signum);
    done = true;
}

float calculateRpmFromPulses(int numPulses) {
    float rpms = numPulses;
    const int numSecondsRecorded = 3;
    rpms /= numSecondsRecorded;
    const float numSpokesPerRevolution = 18.5;
    const float numPulsesPerRev = numSpokesPerRevolution / 4;
    rpms /= numPulsesPerRev;
    rpms *= 60;
    return rpms;
}

int main(int argc, const char *argv) {
    int rval = 0;
    int numPulses, numRead;
    float rpms;
    FILE *tach_file;
    int lcd_fd = -1;
    struct ioctl_message msg;

    struct sigaction action = {0};
    action.sa_handler = trapper;
    sigaction(SIGINT, &action, NULL);

    tach_file = fopen(TACH_FILE_NAME, "rb");
    if (tach_file == NULL) {
        printf("Error opening the tach file\n");
        return -1;
    }

    lcd_fd = open(SCREEN_FILE_NAME, O_WRONLY | O_NDELAY);
    if (lcd_fd <  0) {
        printf("Error opening the LCD file\n");
        rval = -1;
        goto cleanup;
    }

    if (ioctl(lcd_fd, (unsigned int) IOCTL_CLEAR_DISPLAY, &msg) < 0) {
        printf("Error clearing display\n");
        rval = -1; 
        goto cleanup;
    }

    do {
       numRead = fread(&numPulses, 1, sizeof(numPulses), tach_file);
       if (numRead != sizeof(numPulses)) {
           printf("Error reading from gpio tach - %d\n", numRead);
           rval = -2;
           break;
       }

       rpms = calculateRpmFromPulses(numPulses);

       memset(msg.kbuf, '\0', sizeof(msg.kbuf));
       snprintf(msg.kbuf, sizeof(msg.kbuf), "RPMS: %3.1f    ", rpms);

       if (ioctl(lcd_fd, (unsigned int) IOCTL_PRINT_ON_FIRST_LINE, &msg) < 0) {
           printf("Error writing to the LCD\n");
           rval = -3;
           break;
       }

        sleep(1);
    } while (!done);

cleanup:
    if (tach_file != NULL) {
        fclose(tach_file);
    }
    if (lcd_fd >= 0) {
        close(lcd_fd);
    }
    return rval;
}
