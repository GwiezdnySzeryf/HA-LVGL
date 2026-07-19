#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <time.h>
#include <stdint.h>
#include "../lvgl/lvgl.h"

#define WIDTH 480
#define HEIGHT 480
#define BYTES_PER_PIXEL 4
#define SCREEN_SIZE (WIDTH * HEIGHT * BYTES_PER_PIXEL)

static int fb_fd = -1;
static uint32_t *fb_ptr = NULL;

static int touch_fd = -1;
static int touch_x = 0;
static int touch_y = 0;
static int touch_pressed = 0;

/*======================
   SYSTEM TICK PROVIDER
 *======================*/
extern "C" uint32_t custom_tick_get(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

/*======================
   DISPLAY DRIVER (fbdev)
 *======================*/
static void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
    int32_t x, y;
    for (y = area->y1; y <= area->y2; y++) {
        for (x = area->x1; x <= area->x2; x++) {
            // Copy pixels from LVGL internal buffer directly to mapped framebuffer /dev/fb0
            // lv_color_t is 32-bit (ARGB8888) when color depth is 32
            fb_ptr[y * WIDTH + x] = color_p->full;
            color_p++;
        }
    }
    
    // Notify LVGL that flush is finished
    lv_disp_flush_ready(disp_drv);
}

bool hal_display_init(void) {
    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("Error opening /dev/fb0");
        return false;
    }

    fb_ptr = (uint32_t *)mmap(NULL, SCREEN_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_ptr == MAP_FAILED) {
        perror("Error mmaping framebuffer");
        close(fb_fd);
        fb_fd = -1;
        return false;
    }

    // Allocate LVGL display draw buffers
    // We allocate a buffer for 40 rows of pixels (extremely fast and saves RAM)
    static lv_disp_draw_buf_t disp_buf;
    static lv_color_t buf1[WIDTH * 40];
    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, WIDTH * 40);

    // Initialize display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.draw_buf = &disp_buf;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.hor_res = WIDTH;
    disp_drv.ver_res = HEIGHT;
    lv_disp_drv_register(&disp_drv);

    printf("[HAL] Framebuffer display registered successfully. 480x480 bpp=32\n");
    return true;
}

/*======================
   INPUT DRIVER (evdev)
 *======================*/
static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
    struct input_event ev;
    
    // Read all available non-blocking events from /dev/input/event0
    while (read(touch_fd, &ev, sizeof(struct input_event)) > 0) {
        if (ev.type == 3) { // EV_ABS
            if (ev.code == 53) { // ABS_MT_POSITION_X
                touch_x = ev.value;
            } else if (ev.code == 54) { // ABS_MT_POSITION_Y
                touch_y = ev.value;
            } else if (ev.code == 57) { // ABS_MT_TRACKING_ID (finger tracking)
                touch_pressed = (ev.value != -1);
            }
        } else if (ev.type == 1 && ev.code == 330) { // EV_KEY -> BTN_TOUCH (fallback)
            touch_pressed = ev.value;
        }
    }

    data->point.x = touch_x;
    data->point.y = touch_y;
    data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

bool hal_touch_init(void) {
    touch_fd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
    if (touch_fd < 0) {
        perror("Error opening /dev/input/event0");
        return false;
    }

    // Register touchscreen driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    printf("[HAL] Touchscreen driver registered successfully.\n");
    return true;
}

void hal_shutdown(void) {
    if (fb_ptr && fb_ptr != MAP_FAILED) {
        munmap(fb_ptr, SCREEN_SIZE);
    }
    if (fb_fd >= 0) close(fb_fd);
    if (touch_fd >= 0) close(touch_fd);
}
