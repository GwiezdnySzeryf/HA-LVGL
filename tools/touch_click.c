#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

void send_ev(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event ev = {0};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(fd, &ev, sizeof(ev));
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <x> <y>\n", argv[0]);
        return 1;
    }
    int x = atoi(argv[1]);
    int y = atoi(argv[2]);
    int fd = open("/dev/input/event0", O_WRONLY);
    if (fd < 0) {
        perror("open /dev/input/event0");
        return 1;
    }

    // Touch Press
    send_ev(fd, EV_ABS, 53, x);   // ABS_MT_POSITION_X
    send_ev(fd, EV_ABS, 54, y);   // ABS_MT_POSITION_Y
    send_ev(fd, EV_ABS, 57, 100); // ABS_MT_TRACKING_ID (pressed)
    send_ev(fd, EV_KEY, 330, 1);  // BTN_TOUCH
    send_ev(fd, EV_SYN, 0, 0);    // SYN_REPORT

    usleep(80000);                // 80ms short tap for LV_EVENT_CLICKED

    // Touch Release
    send_ev(fd, EV_ABS, 57, -1);  // ABS_MT_TRACKING_ID (released)
    send_ev(fd, EV_KEY, 330, 0);  // BTN_TOUCH
    send_ev(fd, EV_SYN, 0, 0);    // SYN_REPORT

    usleep(50000);

    close(fd);
    return 0;
}
