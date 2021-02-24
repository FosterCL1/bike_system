#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TACH_FILE_NAME "/dev/gpiotach1.0"
#define SCREEN_FILE_NAME "/dev/klcd"
#define TILT_FILE_NAME "/dev/spidev1.0"

bool done = false;

#define IOCTL_CLEAR_DISPLAY '0'
#define IOCTL_PRINT_ON_FIRST_LINE '1'
#define IOCTL_PRINT_ON_SECOND_LINE '2'

#define IOCTL_CURSOR_OFF '5'

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

int readAndPrintRPMsOnFirstLine(FILE *tach_file, int lcd_fd)
{
    struct ioctl_message msg;
    int numPulses;
    int numRead;
    float rpms;

    numRead = fread(&numPulses, 1, sizeof(numPulses), tach_file);
    if (numRead != sizeof(numPulses)) {
        printf("Error reading from gpio tach - %d\n", numRead);
        return -2;
    }

    rpms = calculateRpmFromPulses(numPulses);

    memset(msg.kbuf, '\0', sizeof(msg.kbuf));
    snprintf(msg.kbuf, sizeof(msg.kbuf), "RPMS: %3.1f    ", rpms);

    if (ioctl(lcd_fd, (unsigned int) IOCTL_PRINT_ON_FIRST_LINE, &msg) < 0) {
        printf("Error writing to the LCD\n");
        return -3;
    }
    return 0;
}

int readAngle(int tilt_fd, uint16_t *angle)
{
    static uint8_t tx[2] = {0xff};
    static uint8_t rx[2] = {0};
    int ret;

    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx, 
        .len = 2,
        .delay_usecs = 1,
        .bits_per_word = 8,
        .speed_hz = 500000,
    };

    ret = ioctl(tilt_fd, SPI_IOC_MESSAGE(1), &tr);

    if (ret == 1) {
        printf("Error sending SPI message\n");
        return -1;
    }
    else
    {
        //printf("Raw tilt values: 0x%02x 0x%02x\n", rx[0], rx[1]);
    }

    // The first two bits are alarms - ignore em?
    uint16_t temp = (rx[0] & 0x3F) << 6;
    // The last two bits are error and parity - ignore em?
    temp |= rx[1] >> 2;

    *angle = temp;

    //printf("Calculated angle: %d\n", *angle);

    return 0;
}

int readAndPrintAngleOnSecondLine(int tilt_fd, int lcd_fd)
{
    uint16_t angle;
    struct ioctl_message msg;

    if (readAngle(tilt_fd, &angle) != 0) {
        printf("Error reading the angle\n");
        return -1;
    }

    memset(msg.kbuf, '\0', sizeof(msg.kbuf));
    snprintf(msg.kbuf, sizeof(msg.kbuf), "T: %d", 2800 - angle);

    if (ioctl(lcd_fd, (unsigned int) IOCTL_PRINT_ON_SECOND_LINE, &msg) < 0) {
        printf("Error writing to the LCD\n");
        return -1;
    }
}

int main(int argc, const char *argv) {
    int rval = 0;
    int numRead;
    FILE *tach_file;
    int lcd_fd = -1;
    int tilt_fd = -1;
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
    if (lcd_fd < 0) {
        printf("Error opening the LCD file\n");
        rval = -1;
        goto cleanup;
    }

    if (ioctl(lcd_fd, (unsigned int) IOCTL_CLEAR_DISPLAY, &msg) < 0) {
        printf("Error clearing display\n");
        rval = -1; 
        goto cleanup;
    }

    if (ioctl(lcd_fd, (unsigned int) IOCTL_CURSOR_OFF, &msg) < 0) {
        printf("Error turning off cursor\n");
        rval = -1;
        goto cleanup;
    }

    tilt_fd = open(TILT_FILE_NAME, O_RDWR);
    if (tilt_fd < 0) {
        printf("Error opening TILT file\n");
        rval = -1;
        goto cleanup;
    }

    do {
        if (readAndPrintRPMsOnFirstLine(tach_file, lcd_fd) < 0) {
            goto cleanup;
        }
        
        if (readAndPrintAngleOnSecondLine(tilt_fd, lcd_fd) < 0) {
            goto cleanup;
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
    if (tilt_fd >= 0) {
        close(tilt_fd);
    }
    return rval;
}
