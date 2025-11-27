/*******************************************************************
 *
 * main.c - LVGL simulator for GNU/Linux
 *
 * Based on the original file from the repository
 *
 * @note eventually this file won't contain a main function and will
 * become a library supporting all major operating systems
 *
 * To see how each driver is initialized check the
 * 'src/lib/display_backends' directory
 *
 * - Clean up
 * - Support for multiple backends at once
 *   2025 EDGEMTech Ltd.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
 *
 ******************************************************************/
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "lvgl.h"

#define LEFT_EYE_GIF "A:/mnt/data/panel/normal_left_eye.gif"
#define LEFT_EYELID_GIF "A:/mnt/data/panel/normal_left_eyelid.gif"
#define RIGHT_EYE_GIF "A:/mnt/data/panel/normal_right_eye.gif"
#define RIGHT_EYELID_GIF "A:/mnt/data/panel/normal_right_eyelid.gif"

static void bl_write(const char *path, const char *val) {
  int fd = open(path, O_WRONLY);
  if (fd < 0) return;
  write(fd, val, strlen(val));
  close(fd);
}
static void bl_export_if_needed(int chip, int pwm) {
  char pwm_path[128];
  snprintf(pwm_path, sizeof(pwm_path), "/sys/class/pwm/pwmchip%d/pwm%d", chip,
           pwm);
  if (access(pwm_path, F_OK) == 0) return;
  char export_path[128];
  snprintf(export_path, sizeof(export_path), "/sys/class/pwm/pwmchip%d/export",
           chip);
  char pwm_str[16];
  snprintf(pwm_str, sizeof(pwm_str), "%d", pwm);
  bl_write(export_path, pwm_str);
  usleep(20000);
}
static void bl_config(int chip, int pwm, int period_ns, int duty_ns,
                      int enable) {
  bl_export_if_needed(chip, pwm);
  char path[160];
  snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/enable", chip,
           pwm);
  bl_write(path, "0");
  snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/period", chip,
           pwm);
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", period_ns);
  bl_write(path, buf);
  snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/duty_cycle",
           chip, pwm);
  snprintf(buf, sizeof(buf), "%d", duty_ns);
  bl_write(path, buf);
  snprintf(path, sizeof(path), "/sys/class/pwm/pwmchip%d/pwm%d/enable", chip,
           pwm);
  bl_write(path, enable ? "1" : "0");
}
static void backlight_init_dual(void) {
  bl_config(6, 5, 1000000, 500000, 1);
  bl_config(12, 0, 1000000, 500000, 1);
}

uint32_t custom_tick_get(void) {
  static uint64_t start_ms = 0;
  if (start_ms == 0) {
    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    start_ms = (tv_start.tv_sec * 1000000 + tv_start.tv_usec) / 1000;
  }
  struct timeval tv_now;
  gettimeofday(&tv_now, NULL);
  uint64_t now_ms = (tv_now.tv_sec * 1000000 + tv_now.tv_usec) / 1000;
  return (uint32_t)(now_ms - start_ms);
}

__attribute__((section(".fast_ram"))) lv_color_t buf00[160 * 160];
__attribute__((section(".fast_ram"))) lv_color_t buf01[160 * 160];
__attribute__((section(".fast_ram"))) lv_color_t buf10[160 * 160];
__attribute__((section(".fast_ram"))) lv_color_t buf11[160 * 160];

static void show_blink_eye(lv_disp_t *disp, const char *eye_path,
                           const char *lid_path) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  // 眼球（底层）
  lv_obj_t *gif_eye = lv_gif_create(scr);
  lv_gif_set_src(gif_eye, eye_path);
  lv_obj_center(gif_eye);

  // 眼睑（顶层，透明区域自动透出眼球）
  lv_obj_t *gif_lid = lv_gif_create(scr);
  lv_gif_set_src(gif_lid, lid_path);
  lv_obj_center(gif_lid);
}

int main(int argc, char **argv) {
  lv_init();
  backlight_init_dual();
  lv_tick_set_cb(custom_tick_get);

  lv_display_t *disp0 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp0, "/dev/fb0");
  lv_display_set_resolution(disp0, 160, 160);
  lv_display_set_color_format(disp0, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp0, buf00, buf01, sizeof(buf00),
                         LV_DISPLAY_RENDER_MODE_FULL);

  lv_display_t *disp1 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp1, "/dev/fb1");
  lv_display_set_resolution(disp1, 160, 160);
  lv_display_set_color_format(disp1, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp1, buf10, buf11, sizeof(buf10),
                         LV_DISPLAY_RENDER_MODE_FULL);

  show_blink_eye(disp0, LEFT_EYE_GIF, LEFT_EYELID_GIF);
  show_blink_eye(disp1, RIGHT_EYE_GIF, RIGHT_EYELID_GIF);

  while (1) {
    static uint32_t last_fps_time = 0;
    static uint32_t frame_counter = 0;

    uint32_t now = lv_tick_get();

    lv_timer_handler();  // 原来的

    frame_counter++;

    // 每 500ms 更新一次 FPS 显示（你可以不要这个 if，直接打印也行）
    if (now - last_fps_time >= 500) {
      float fps = frame_counter * 1000.0f / (now - last_fps_time);
      printf("FPS: %.1f\n", fps);

      frame_counter = 0;
      last_fps_time = now;
    }
    usleep(8000);
  }

  return 0;
}
