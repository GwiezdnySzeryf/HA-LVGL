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
    if (argc < 5) {
        printf("Usage: %s <x1> <y1> <x2> <y2>\n", argv[0]);
        return 1;
    }
    int x1 = atoi(argv[1]);
    int y1 = atoi(argv[2]);
    int x2 = atoi(argv[3]);
    int y2 = atoi(argv[4]);

    int fd = open("/dev/input/event0", O_WRONLY);
    if (fd < 0) {
        perror("open /dev/input/event0");
        return 1;
    }

    // Touch Press
    send_ev(fd, EV_ABS, 53, x1);   // ABS_MT_POSITION_X
    send_ev(fd, EV_ABS, 54, y1);   // ABS_MT_POSITION_Y
    send_ev(fd, EV_ABS, 57, 100);  // ABS_MT_TRACKING_ID
    send_ev(fd, EV_KEY, 330, 1);   // BTN_TOUCH
    send_ev(fd, EV_SYN, 0, 0);     // SYN_REPORT
    usleep(50000);

    int steps = 15;
    for (int i = 1; i <= steps; ++i) {
        int cx = x1 + (x2 - x1) * i / steps;
        int cy = y1 + (y2 - y1) * i / steps;
        send_ev(fd, EV_ABS, 53, cx);
        send_ev(fd, EV_ABS, 54, cy);
        send_ev(fd, EV_SYN, 0, 0);
        usleep(20000);
    }

    // Touch Release
    send_ev(fd, EV_ABS, 57, -1);
    send_ev(fd, EV_KEY, 330, 0);
    send_ev(fd, EV_SYN, 0, 0);

    close(fd);
    return 0;
}
