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

/* ==================== 执行单次眨眼 ==================== */
static void perform_single_blink(struct eye_t *eye) {
  if (!eye || !eye->eyelid_gif) return;
  // 重新开始播放眼睑GIF
  lv_gif_set_loop_count(eye->eyelid_gif, 1);  // 只播放一次
  lv_gif_resume(eye->eyelid_gif);
}

/* ==================== 眨眼定时器回调 ==================== */
static void blink_timer_cb(lv_timer_t *timer) {
  struct eye_t *eye = lv_timer_get_user_data(timer);
  if (!eye || !eye->eyelid_gif) return;

  /* 如果是无限眨眼 或 还有剩余次数 */
  if (eye->blink_remaining == -1 || eye->blink_remaining > 0) {
    perform_single_blink(eye);

    if (eye->blink_remaining > 0) {
      eye->blink_remaining--;
      if (eye->blink_remaining == 0) {
        lv_timer_pause(timer);  // 次数用完自动暂停
      }
    }
  }
}

/* ==================== 创建眼睛 ==================== */
static void eye_create(lv_disp_t *disp, struct eye_t *eye,
                       const char *eye_gif_path, const char *eyelid_gif_path,
                       int32_t max_offset, uint32_t blink_interval,
                       int32_t blink_remaining) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  eye->disp = disp;
  eye->max_offset = max_offset;
  eye->blink_remaining = blink_remaining;
  eye->blink_interval = blink_interval;

  eye->eye_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eye_gif, eye_gif_path);
  lv_obj_center(eye->eye_gif);

  eye->eyelid_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eyelid_gif, eyelid_gif_path);
  lv_obj_center(eye->eyelid_gif);
  lv_gif_pause(eye->eyelid_gif);

  // 创建眨眼定时器（初始为暂停状态）
  eye->blink_timer = lv_timer_create(blink_timer_cb, eye->blink_interval, eye);
  lv_timer_pause(eye->blink_timer);

  if (0 == blink_remaining) {  // 不持续眨眼
  } else if (blink_remaining > 0) {
    lv_gif_set_loop_count(eye->eyelid_gif, 1);
    if (1 != blink_remaining) {
      if (blink_interval == 0) {
        lv_gif_set_loop_count(eye->eyelid_gif, blink_remaining);
      } else {
        eye->blink_remaining++;  // 用于timer cb
        // 每次眨眼间隔一段时间，在timer cb里处理参数 blink_remaining
        lv_timer_set_period(eye->blink_timer, blink_interval);
        lv_timer_resume(eye->blink_timer);
      }
    }
    lv_gif_resume(eye->eyelid_gif);
  } else {  // -1 一直眨眼
    if (blink_interval) {
      lv_gif_set_loop_count(eye->eyelid_gif, 1);
      lv_timer_set_period(eye->blink_timer, blink_interval);
      lv_timer_resume(eye->blink_timer);
      lv_gif_resume(eye->eyelid_gif);
    }
  }
}

/* ==================== 眨眼控制函数 ==================== */
/* ==================== 眨眼控制函数（完全重构版，逻辑与 eye_create 完全一致）
 * ==================== */
void eye_blink(struct eye_t *eye, uint32_t interval_ms, int32_t count) {
  if (!eye || !eye->blink_timer || !eye->eyelid_gif) return;

  /* 1. 先停掉当前所有眨眼行为 */
  lv_timer_pause(eye->blink_timer);
  lv_gif_pause(eye->eyelid_gif);

  /* 2. 更新参数 */
  eye->blink_interval = interval_ms;
  eye->blink_remaining = count;  // 直接赋值，后面会根据情况处理

  /* 3. 设置 GIF 为单次播放（无论多少次，都每次只播一次，由定时器触发） */
  lv_gif_set_loop_count(eye->eyelid_gif, 1);

  /* 4. 分类处理三种情况（和 eye_create 完全一致的逻辑） */
  if (count == 0) {
    /* 不眨眼：什么都不做，保持暂停 */
    return;
  } else if (count > 0) {
    /* 眨眼有限次数 */
    if (interval_ms == 0) {
      /* 间隔为0 → 连续播放 count 次（不推荐，基本不用） */
      lv_gif_set_loop_count(eye->eyelid_gif, count);
      lv_gif_restart(eye->eyelid_gif);  // 从头开始播 count 次
    } else {
      /* 有间隔 → 用定时器控制，每次触发播一次 */
      lv_timer_set_period(eye->blink_timer, interval_ms);
      lv_timer_resume(eye->blink_timer);  // 启动定时器

      /* 第一次立即触发一次（与 eye_create 行为一致） */
      perform_single_blink(eye);
      eye->blink_remaining--;  // 已经眨了一次
    }
  } else {  // count == -1
    /* 无限眨眼 */
    if (interval_ms == 0) {
      /* 间隔为0 → GIF 无限循环（极少用） */
      lv_gif_set_loop_count(eye->eyelid_gif, 0);  // 0 = 无限循环
      lv_gif_restart(eye->eyelid_gif);
    } else {
      /* 正常无限眨眼：定时器每隔 interval_ms 触发一次 */
      lv_timer_set_period(eye->blink_timer, interval_ms);
      lv_timer_resume(eye->blink_timer);

      /* 立即眨一次（与 eye_create 一致） */
      perform_single_blink(eye);
      eye->blink_remaining = -1;  // 保持 -1
    }
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
  if (eye_gif_path) {
    lv_gif_set_src(eye->eye_gif, eye_gif_path);
    lv_obj_center(eye->eye_gif);
    /* 重置位置 */
    lv_obj_set_style_translate_x(eye->eye_gif, 0, 0);
    lv_obj_set_style_translate_y(eye->eye_gif, 0, 0);
  }
  if (eyelid_gif_path) {
    lv_gif_set_src(eye->eyelid_gif, eyelid_gif_path);
    lv_obj_center(eye->eyelid_gif);
  }

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

  // eye_create(disp0, left_eye, left_eye_path, left_eyelid_path, max_offset_px,
  //            2000, -1);
  // eye_create(disp1, right_eye, right_eye_path, right_eyelid_path,
  // max_offset_px,
  //            2000, -1); // 间隔2s无限循环眨眼

  eye_create(disp0, left_eye, left_eye_path, left_eyelid_path, max_offset_px,
             2000, 0);
  eye_create(disp1, right_eye, right_eye_path, right_eyelid_path, max_offset_px,
             2000, 0);  // 不眨眼

  // eye_blink(left_eye, 2000, 3); // 间隔2s眨眼3次
  // eye_blink(right_eye, 2000, 3);

  eye_blink(left_eye, 2000, -1);  // 间隔2s无限循环眨眼
  eye_blink(right_eye, 2000, -1);
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
