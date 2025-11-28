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
#include "eye_controller.h"

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

#define SCREEN_DIAMETER 240  // px

__attribute__((section(".fast_ram")))
lv_color_t buf00[SCREEN_DIAMETER * SCREEN_DIAMETER / 2];
__attribute__((section(".fast_ram")))
lv_color_t buf01[SCREEN_DIAMETER * SCREEN_DIAMETER / 2];

// static struct eye_t left_eye = {0};
// static struct eye_t right_eye = {0};

static void eyelid_anim_completed(lv_anim_t *anim) {
  struct eye_t *eye = (struct eye_t *)anim->user_data;
  if (eye) {
    eye->is_blinking = false;

    // 暂停眼睑动画并重置到第一帧
    lv_gif_pause(eye->eyelid_gif);
    lv_gif_restart(eye->eyelid_gif);

    // 如果还有剩余的眨眼次数，继续安排下一次眨眼
    if (eye->blink_remaining != 0) {
      if (eye->blink_remaining > 0) {
        eye->blink_remaining--;
      }
      // -1 表示无限眨眼，不需要减少
    }
  }
}

/* ==================== 执行单次眨眼 ==================== */
static void perform_single_blink(struct eye_t *eye) {
  if (!eye || !eye->eyelid_gif || eye->is_blinking) return;

  eye->is_blinking = true;

  // 重新开始播放眼睑GIF
  lv_gif_restart(eye->eyelid_gif);
  lv_gif_set_loop_count(eye->eyelid_gif, 1);  // 只播放一次
  lv_gif_resume(eye->eyelid_gif);

  // 创建动画来监测眨眼完成（通过GIF的循环计数或定时器）
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, eye);
  lv_anim_set_exec_cb(&anim, NULL);  // 不需要执行回调，只用于计时
  lv_anim_set_values(&anim, 0, 100);
  lv_anim_set_time(&anim, 200);  // 假设眨眼动画持续200ms
  lv_anim_set_ready_cb(&anim, eyelid_anim_completed);
  lv_anim_set_user_data(&anim, eye);
  lv_anim_start(&anim);
}

/* ==================== 眨眼定时器回调 ==================== */
static void blink_timer_cb(lv_timer_t *timer) {
  struct eye_t *eye = lv_timer_get_user_data(timer);
  if (!eye) return;

  // 如果还有剩余的眨眼次数或设置为无限眨眼
  if (eye->blink_remaining == -1 || eye->blink_remaining > 0) {
    perform_single_blink(eye);
  }

  // 如果眨眼次数用完，停止定时器
  if (eye->blink_remaining == 0) {
    lv_timer_pause(eye->blink_timer);
  }
}

/* ==================== 创建眼睛 ==================== */
static void eye_create(lv_disp_t *disp, struct eye_t *eye,
                       const char *eye_gif_path, const char *eyelid_gif_path,
                       int32_t max_offset) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  eye->disp = disp;

  eye->eye_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eye_gif, eye_gif_path);
  lv_obj_center(eye->eye_gif);

  eye->eyelid_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eyelid_gif, eyelid_gif_path);
  lv_obj_center(eye->eyelid_gif);

  /* 初始状态：暂停 + 只播放一轮（安全） */
  lv_gif_pause(eye->eyelid_gif);
  lv_gif_set_loop_count(eye->eyelid_gif, 1);

  eye->max_offset = max_offset;
  eye->blink_remaining = 0;
  eye->blink_interval = 3000;  // 默认3秒眨眼一次
  eye->is_blinking = false;

  // 创建眨眼定时器（初始为暂停状态）
  eye->blink_timer = lv_timer_create(blink_timer_cb, eye->blink_interval, eye);
  lv_timer_pause(eye->blink_timer);
}

/* ==================== 眨眼控制函数 ==================== */
void eye_blink(struct eye_t *eye, uint32_t interval_ms, int32_t count) {
  if (!eye || !eye->blink_timer) return;

  // 停止当前的眨眼
  lv_timer_pause(eye->blink_timer);

  // 设置新的眨眼参数
  eye->blink_interval = interval_ms;
  eye->blink_remaining = count;

  // 更新定时器周期
  lv_timer_set_period(eye->blink_timer, interval_ms);

  // 如果还有眨眼次数，启动定时器
  if (count != 0) {
    lv_timer_resume(eye->blink_timer);
  }
}

/* ==================== 立即眨眼一次 ==================== */
void eye_blink_once(struct eye_t *eye) {
  if (!eye) return;
  perform_single_blink(eye);
}

/* ==================== 2. 视线追随（眼球动图整体平移） ==================== */
static void look_at_anim_x(void *obj, int32_t v) {
  lv_obj_set_style_translate_x(obj, v, 0);
}

static void look_at_anim_y(void *obj, int32_t v) {
  lv_obj_set_style_translate_y(obj, v, 0);
}
void eye_look_at(struct eye_t *eye, int32_t tx, int32_t ty) {
  if (!eye || !eye->eye_gif) return;

  int32_t x = tx;
  int32_t y = ty;
  if (x > eye->max_offset) x = eye->max_offset;
  if (x < -eye->max_offset) x = -eye->max_offset;
  if (y > eye->max_offset) y = eye->max_offset;
  if (y < -eye->max_offset) y = -eye->max_offset;

  /* X 轴动画 */
  lv_anim_t ax;
  lv_anim_init(&ax);
  lv_anim_set_var(&ax, eye->eye_gif);
  lv_anim_set_values(&ax, lv_obj_get_style_translate_x(eye->eye_gif, 0), x);
  lv_anim_set_exec_cb(&ax, look_at_anim_x);
  lv_anim_set_time(&ax, 180);
  lv_anim_set_path_cb(&ax, lv_anim_path_ease_out);
  lv_anim_start(&ax);

  /* Y 轴动画 */
  lv_anim_t ay;
  lv_anim_init(&ay);
  lv_anim_set_var(&ay, eye->eye_gif);
  lv_anim_set_values(&ay, lv_obj_get_style_translate_y(eye->eye_gif, 0), y);
  lv_anim_set_exec_cb(&ay, look_at_anim_y);
  lv_anim_set_time(&ay, 180);
  lv_anim_set_path_cb(&ay, lv_anim_path_ease_out);
  lv_anim_start(&ay);
}

/* ==================== 3. 切换整套眼睛素材 ==================== */
void eye_switch_material(struct eye_t *eye, const char *eye_gif_path,
                         const char *eyelid_gif_path, int32_t max_offset_px) {
  lv_gif_set_src(eye->eye_gif, eye_gif_path);
  lv_gif_set_src(eye->eyelid_gif, eyelid_gif_path);

  lv_obj_center(eye->eye_gif);
  lv_obj_center(eye->eyelid_gif);

  /* 重置位置 */
  lv_obj_set_style_translate_x(eye->eye_gif, 0, 0);
  lv_obj_set_style_translate_y(eye->eye_gif, 0, 0);

  eye->max_offset = max_offset_px;

  /* 看向正前方 */
  eye_look_at(eye, 0, 0);
}

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

static void print_fps(void) {
  static uint32_t last_fps_time = 0;
  static uint32_t frame_counter = 0;

  uint32_t now = lv_tick_get();

  frame_counter++;

  // 每 500ms 更新一次 FPS 显示
  if (now - last_fps_time >= 1000) {
    float fps = frame_counter * 1000.0f / (now - last_fps_time);
    printf("FPS: %.1f\n", fps);
    frame_counter = 0;
    last_fps_time = now;
  }
}

void eye_controller_init(struct eye_t *left_eye, struct eye_t *right_eye,
                         char *left_eye_path, char *left_eyelid_path,
                         char *right_eye_path, char *right_eyelid_path,
                         uint32_t max_offset_px) {
  lv_init();
  backlight_init_dual();
  lv_tick_set_cb(custom_tick_get);

  lv_display_t *disp0 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp0, "/dev/fb0");
  lv_display_set_resolution(disp0, SCREEN_DIAMETER, SCREEN_DIAMETER);
  lv_display_set_color_format(disp0, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp0, buf00, buf01, sizeof(buf00),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  lv_display_t *disp1 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp1, "/dev/fb1");
  lv_display_set_resolution(disp1, SCREEN_DIAMETER, SCREEN_DIAMETER);
  lv_display_set_color_format(disp1, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp1, buf00, buf01, sizeof(buf00),
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  eye_create(disp0, left_eye, left_eye_path, left_eyelid_path, max_offset_px);
  eye_create(disp1, right_eye, right_eye_path, right_eyelid_path,
             max_offset_px);
}

/**
 * @brief 销毁单个眼睛对象
 * @param eye 眼睛结构体指针
 */
void eye_destroy(struct eye_t *eye) {
  if (!eye) return;

  // 停止并删除眨眼定时器
  if (eye->blink_timer) {
    lv_timer_del(eye->blink_timer);
    eye->blink_timer = NULL;
  }

  // 删除眼球GIF对象
  if (eye->eye_gif) {
    lv_obj_del(eye->eye_gif);
    eye->eye_gif = NULL;
  }

  // 删除眼睑GIF对象
  if (eye->eyelid_gif) {
    lv_obj_del(eye->eyelid_gif);
    eye->eyelid_gif = NULL;
  }

  // 重置结构体状态
  eye->max_offset = 0;
  eye->blink_remaining = 0;
  eye->blink_interval = 0;
  eye->is_blinking = false;
}

void eye_controller_deinit(struct eye_t *left_eye, struct eye_t *right_eye) {
  if (!left_eye || !right_eye) return;

  // 停止所有眼睛活动
  if (left_eye->blink_timer) {
    lv_timer_pause(left_eye->blink_timer);
  }
  if (right_eye->blink_timer) {
    lv_timer_pause(right_eye->blink_timer);
  }

  // 处理一次LVGL任务循环，确保所有删除操作完成
  lv_timer_handler();
  usleep(10000);  // 10ms延迟确保操作完成

  // 销毁眼睛对象
  eye_destroy(left_eye);
  eye_destroy(right_eye);

  // LVGL反初始化
  lv_deinit();
}

void eye_controller_task(void) {
  while (1) {
    lv_timer_handler();
    print_fps();
    usleep(8000);
  }
}
