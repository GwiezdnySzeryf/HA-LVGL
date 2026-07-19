/**
 * @file lv_conf.h
 * Configuration file for LVGL v8.3
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/

/* Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888) */
#define LV_COLOR_DEPTH 32

/* Swap the 2 bytes of RGB565 color. Useful if the display has a 8-bit interface (e.g. SPI) */
#define LV_COLOR_16_SWAP 0

/* Enable features draw state transparency */
#define LV_COLOR_SCREEN_TRANSP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/

/* 1: use custom malloc/free, 0: use the built-in `lv_mem_alloc` and `lv_mem_free` */
#define LV_MEM_CUSTOM 0

#if LV_MEM_CUSTOM == 0
    /* Size of the memory adrpress space for lv_mem_alloc in bytes (greater than 2kB) */
    #define LV_MEM_SIZE (128U * 1024U)          /* [bytes] */
    #define LV_MEM_ADR 0
#else
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>   /* Header for the custom alloc/free functions */
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

/* Use the standard `memcpy` and `memset` instead of LVGL's own functions */
#define LV_MEMCPY_MEMSET_STD 1

/*====================
   HAL SETTINGS
 *====================*/

/* Default display refresh period. LVGL will redraw the screen with this period */
#define LV_DISP_DEF_REFRESH_PERIOD 30      /* [ms] */

/* Input device read period in milliseconds */
#define LV_INDEV_DEF_READ_PERIOD 30        /* [ms] */

/* Use a custom tick source */
#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE <stdint.h>
    uint32_t custom_tick_get(void);
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (custom_tick_get())
#endif

/*=======================
   FEAT_ANIMATION & WIDGETS
 *=======================*/

#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW 1
#define LV_USE_BLEND_COLOR_FILTER 1

/*======================
   FONT USAGE
 *======================*/

/* Montserrat fonts */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1

/* Set the default font */
#define LV_FONT_DEFAULT &lv_font_montserrat_16

/*======================
   WIDGETS CONFIGURATION
 *======================*/

#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     1
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1

/*======================
   EXTRA LIBRARIES
 *======================*/

#define LV_USE_QRCODE     1

#endif /*LV_CONF_H*/
