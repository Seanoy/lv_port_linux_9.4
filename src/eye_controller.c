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
lv_color_t buf00[SCREEN_DIAMETER * SCREEN_DIAMETER];
__attribute__((section(".fast_ram")))
lv_color_t buf01[SCREEN_DIAMETER * SCREEN_DIAMETER];

// 全局眼皮控制器实例
static eyelid_controller_t g_eyelid_controller = {0};

static void _gif_reset_and_play(lv_obj_t *gif, int32_t loop_count,
                                bool resume) {
  if (!gif) return;
  lv_gif_restart(gif);
  lv_gif_pause(gif);
  lv_gif_set_loop_count(gif, loop_count);  // n 或 -1（无限）
  if (resume) {
    lv_gif_resume(gif);
  }
}

/* ==================== 执行单次眨眼（眼皮） ==================== */
static void perform_single_eyelid_blink(struct eye_t *eye) {
  if (!eye || !eye->eyelid_gif) return;
  _gif_reset_and_play(eye->eyelid_gif, 1, true);
}

/* ==================== 统一的眼皮眨眼定时器回调 ==================== */
static void unified_eyelid_blink_timer_cb(lv_timer_t *timer) {
  eyelid_controller_t *controller = lv_timer_get_user_data(timer);
  if (!controller) return;

  /* 如果是无限眨眼 或 还有剩余次数 */
  if (controller->blink_remaining == -1 || controller->blink_remaining > 0) {
    // 同步控制两个眼皮：先都准备好，再一起恢复
    if (controller->left_eye && controller->left_eye->eyelid_gif) {
      _gif_reset_and_play(controller->left_eye->eyelid_gif, 1, false);
    }
    if (controller->right_eye && controller->right_eye->eyelid_gif) {
      _gif_reset_and_play(controller->right_eye->eyelid_gif, 1, false);
    }

    // 两个眼皮一起恢复播放（确保同步）
    if (controller->left_eye && controller->left_eye->eyelid_gif) {
      lv_gif_resume(controller->left_eye->eyelid_gif);
    }
    if (controller->right_eye && controller->right_eye->eyelid_gif) {
      lv_gif_resume(controller->right_eye->eyelid_gif);
    }

    if (controller->blink_remaining > 0) {
      controller->blink_remaining--;
      if (controller->blink_remaining == 0) {
        lv_timer_pause(timer);  // 次数用完自动暂停
      }
    }
  }
}
/* 事件回调：只要任意一只眼球 GIF 播放完一圈，就强制另一只同步 */
static void eye_gif_sync_event_cb(lv_event_t *e) {
  lv_obj_t *triggered_gif = lv_event_get_target(e);

  lv_obj_t *other_gif = (triggered_gif == g_eyelid_controller.left_eye->eye_gif)
                            ? g_eyelid_controller.right_eye->eye_gif
                            : g_eyelid_controller.left_eye->eye_gif;

  if (!other_gif) return;
  lv_gif_restart(other_gif);
}
/* ==================== 创建眼睛（移除独立的眨眼定时器） ==================== */
static void eye_create(lv_disp_t *disp, struct eye_t *eye,
                       const char *eye_gif_path, const char *eyelid_gif_path,
                       int32_t max_offset) {
  lv_obj_t *scr = lv_disp_get_scr_act(disp);

  eye->disp = disp;
  eye->max_offset = max_offset;

  lv_obj_t *bg = lv_obj_create(scr);
  lv_obj_set_size(bg, LV_PCT(240), LV_PCT(240));
  lv_obj_set_style_bg_color(bg, lv_color_make(203, 198, 193), 0);  // 眼底色
  lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
  lv_obj_move_background(bg);  // 确保在最底层

  eye->eye_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eye_gif, eye_gif_path);
  lv_obj_center(eye->eye_gif);
  lv_obj_add_event_cb(eye->eye_gif, eye_gif_sync_event_cb, LV_EVENT_READY,
                      NULL);

  eye->eyelid_gif = lv_gif_create(scr);
  lv_gif_set_src(eye->eyelid_gif, eyelid_gif_path);
  lv_obj_center(eye->eyelid_gif);
  lv_gif_pause(eye->eyelid_gif);
}

/* ==================== 统一的眼皮眨眼控制函数 ==================== */
void eyelid_blink(uint32_t interval_ms, int32_t count) {
  eyelid_controller_t *controller = &g_eyelid_controller;
  if (!controller->left_eye && !controller->right_eye) return;

  /* 1. 先停掉当前所有眨眼行为 */
  if (controller->blink_timer) {
    lv_timer_pause(controller->blink_timer);
  }

  // 暂停两个眼皮的动画
  if (controller->left_eye && controller->left_eye->eyelid_gif) {
    lv_gif_pause(controller->left_eye->eyelid_gif);
  }
  if (controller->right_eye && controller->right_eye->eyelid_gif) {
    lv_gif_pause(controller->right_eye->eyelid_gif);
  }

  /* 2. 更新控制器参数 */
  controller->blink_interval = interval_ms;
  controller->blink_remaining = count;

  /* 3. 创建或获取定时器 */
  if (!controller->blink_timer) {
    controller->blink_timer =
        lv_timer_create(unified_eyelid_blink_timer_cb, interval_ms, controller);
  } else {
    lv_timer_set_period(controller->blink_timer, interval_ms);
  }

  /* 4. 分类处理三种情况（修正顺序） */
  if (count == 0) {
    /* 不眨眼：什么都不做，保持暂停 */
    return;
  } else if (count > 0) {
    if (interval_ms == 0) {
      /* 间隔为0 → 连续播放 count 次 */
      if (controller->left_eye && controller->left_eye->eyelid_gif) {
        _gif_reset_and_play(controller->left_eye->eyelid_gif, count, true);
      }
      if (controller->right_eye && controller->right_eye->eyelid_gif) {
        _gif_reset_and_play(controller->right_eye->eyelid_gif, count, true);
      }
    } else {
      /* 第一次立即触发一次（使用修正后的顺序） */
      if (controller->left_eye && controller->left_eye->eyelid_gif) {
        _gif_reset_and_play(controller->left_eye->eyelid_gif, 1, true);
      }
      if (controller->right_eye && controller->right_eye->eyelid_gif) {
        _gif_reset_and_play(controller->right_eye->eyelid_gif, 1, true);
      }
      controller->blink_remaining--;            // 已经眨了一次
      lv_timer_resume(controller->blink_timer); /* 有间隔 → 用定时器控制 */
    }
  } else {  // count == -1
    /* 无限眨眼 */
    if (interval_ms == 0) {
      /* 间隔为0 → GIF 无限循环 */
      if (controller->left_eye && controller->left_eye->eyelid_gif) {
        lv_gif_restart(controller->left_eye->eyelid_gif);
      }
      if (controller->right_eye && controller->right_eye->eyelid_gif) {
        lv_gif_restart(controller->right_eye->eyelid_gif);
      }
    } else {
      /* 立即眨一次（使用修正后的顺序） */
      if (controller->left_eye && controller->left_eye->eyelid_gif) {
        _gif_reset_and_play(controller->left_eye->eyelid_gif, 1, true);
      }
      if (controller->right_eye && controller->right_eye->eyelid_gif) {
        _gif_reset_and_play(controller->right_eye->eyelid_gif, 1, true);
      }
      /* 正常无限眨眼：定时器控制 */
      lv_timer_resume(controller->blink_timer);
    }
  }
}

/* ==================== 立即眼皮眨眼一次 ==================== */
void eyelid_blink_once(void) {
  eyelid_controller_t *controller = &g_eyelid_controller;

  // 左眼皮准备
  if (controller->left_eye && controller->left_eye->eyelid_gif) {
    _gif_reset_and_play(controller->left_eye->eyelid_gif, 1, false);
  }

  // 右眼皮准备
  if (controller->right_eye && controller->right_eye->eyelid_gif) {
    _gif_reset_and_play(controller->right_eye->eyelid_gif, 1, false);
  }

  // 两个眼皮一起恢复（确保同步）
  if (controller->left_eye && controller->left_eye->eyelid_gif) {
    lv_gif_resume(controller->left_eye->eyelid_gif);
  }
  if (controller->right_eye && controller->right_eye->eyelid_gif) {
    lv_gif_resume(controller->right_eye->eyelid_gif);
  }
}

void left_eyelid_blink_once(void) {
  if (g_eyelid_controller.left_eye &&
      g_eyelid_controller.left_eye->eyelid_gif) {
    perform_single_eyelid_blink(g_eyelid_controller.left_eye);
  }
}

void right_eyelid_blink_once(void) {
  if (g_eyelid_controller.right_eye &&
      g_eyelid_controller.right_eye->eyelid_gif) {
    perform_single_eyelid_blink(g_eyelid_controller.right_eye);
  }
}

/* ==================== 视线追随函数保持不变 ==================== */
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

// 保持独立控制眼球的函数
void left_eye_look_at(int32_t tx, int32_t ty) {
  eyelid_controller_t *controller = &g_eyelid_controller;
  if (controller->left_eye) {
    eye_look_at(controller->left_eye, tx, ty);
  }
}

void right_eye_look_at(int32_t tx, int32_t ty) {
  eyelid_controller_t *controller = &g_eyelid_controller;
  if (controller->right_eye) {
    eye_look_at(controller->right_eye, tx, ty);
  }
}

/* ==================== 3. 切换整套眼睛素材 ==================== */
typedef struct {
  struct eye_t *left_eye;
  const char *left_eye_gif_path;
  const char *left_eyelid_gif_path;
  int32_t left_max_offset_px;
  struct eye_t *right_eye;
  const char *right_eye_gif_path;
  const char *right_eyelid_gif_path;
  int32_t right_max_offset_px;
} switch_material_data_t;

static void _switch_material_async(void *user_data) {
  switch_material_data_t *data = user_data;

  if (!data || !(data->left_eye && data->right_eye)) return;

  // 现在已经处于 LVGL 主线程，安全操作
  if (data->left_eye) {
    if (data->left_eye_gif_path) {
      lv_gif_set_src(data->left_eye->eye_gif, data->left_eye_gif_path);
      lv_obj_set_style_translate_x(data->left_eye->eye_gif, 0, 0);
      lv_obj_set_style_translate_y(data->left_eye->eye_gif, 0, 0);
    }
    if (data->left_eyelid_gif_path) {
      lv_gif_set_src(data->left_eye->eyelid_gif, data->left_eyelid_gif_path);
      lv_gif_pause(data->left_eye->eyelid_gif);
    }

    data->left_eye->max_offset = data->left_max_offset_px;
    eye_look_at(data->left_eye, 0, 0);
  }

  if (data->right_eye) {
    if (data->right_eye_gif_path) {
      lv_gif_set_src(data->right_eye->eye_gif, data->right_eye_gif_path);
      lv_obj_set_style_translate_x(data->right_eye->eye_gif, 0, 0);
      lv_obj_set_style_translate_y(data->right_eye->eye_gif, 0, 0);
    }
    if (data->right_eyelid_gif_path) {
      lv_gif_set_src(data->right_eye->eyelid_gif, data->right_eyelid_gif_path);
      lv_gif_pause(data->right_eye->eyelid_gif);
    }

    data->right_eye->max_offset = data->right_max_offset_px;
    eye_look_at(data->right_eye, 0, 0);
  }

  free(data);
}

void eye_switch_material(struct eye_t *left_eye, struct eye_t *right_eye,
                         const char *left_eye_gif_path,
                         const char *left_eyelid_gif_path,
                         int32_t left_max_offset_px,
                         const char *right_eye_gif_path,
                         const char *right_eyelid_gif_path,
                         int32_t right_max_offset_px) {
  // 必须异步投递到 LVGL 主线程
  switch_material_data_t *data = malloc(sizeof(*data));
  if (!data) return;

  data->left_eye = left_eye;
  data->left_eye_gif_path = left_eye_gif_path;
  data->left_eyelid_gif_path = left_eyelid_gif_path;
  data->left_max_offset_px = left_max_offset_px;

  data->right_eye = right_eye;
  data->right_eye_gif_path = right_eye_gif_path;
  data->right_eyelid_gif_path = right_eyelid_gif_path;
  data->right_max_offset_px = right_max_offset_px;

  lv_async_call(_switch_material_async, data);
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

void eye_controller_init(
    struct eye_t *left_eye, struct eye_t *right_eye, const char *left_eye_path,
    const char *left_eyelid_path, lv_display_rotation_t rotation_left,
    const char *right_eye_path, const char *right_eyelid_path,
    lv_display_rotation_t rotation_right, uint32_t max_offset_px) {
  lv_init();
  backlight_init_dual();
  lv_tick_set_cb(custom_tick_get);

  lv_display_t *disp0 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp0, "/dev/fb0");
  lv_display_set_resolution(disp0, SCREEN_DIAMETER, SCREEN_DIAMETER);
  lv_display_set_rotation(disp0, rotation_left);
  lv_display_set_color_format(disp0, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp0, buf00, NULL, sizeof(buf00),
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  lv_display_t *disp1 = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp1, "/dev/fb1");
  lv_display_set_resolution(disp1, SCREEN_DIAMETER, SCREEN_DIAMETER);
  lv_display_set_rotation(disp1, rotation_right);
  lv_display_set_color_format(disp1, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(disp1, buf01, NULL, sizeof(buf01),
                         LV_DISPLAY_RENDER_MODE_DIRECT);

  // 初始化眼睛对象（不再创建独立定时器）
  eye_create(disp0, left_eye, left_eye_path, left_eyelid_path, max_offset_px);
  eye_create(disp1, right_eye, right_eye_path, right_eyelid_path,
             max_offset_px);

  // 初始化眼皮控制器
  g_eyelid_controller.left_eye = left_eye;
  g_eyelid_controller.right_eye = right_eye;
  g_eyelid_controller.blink_timer = NULL;
  g_eyelid_controller.blink_interval = 0;
  g_eyelid_controller.blink_remaining = 0;

  // 使用统一眼皮眨眼控制
  eyelid_blink(2000, -1);  // 间隔2秒无限眨眼
}

/**
 * @brief 销毁单个眼睛对象
 */
void eye_destroy(struct eye_t *eye) {
  if (!eye) return;

  // 删除GIF对象
  if (eye->eye_gif) {
    lv_obj_del(eye->eye_gif);
    eye->eye_gif = NULL;
  }

  if (eye->eyelid_gif) {
    lv_obj_del(eye->eyelid_gif);
    eye->eyelid_gif = NULL;
  }

  // 重置状态
  eye->max_offset = 0;
}

void eye_controller_deinit(void) {
  eyelid_controller_t *controller = &g_eyelid_controller;

  // 停止统一眨眼定时器
  if (controller->blink_timer) {
    lv_timer_del(controller->blink_timer);
    controller->blink_timer = NULL;
  }

  // 销毁眼睛对象
  if (controller->left_eye) {
    eye_destroy(controller->left_eye);
    controller->left_eye = NULL;
  }
  if (controller->right_eye) {
    eye_destroy(controller->right_eye);
    controller->right_eye = NULL;
  }

  // LVGL反初始化
  lv_deinit();
}

void eye_controller_task(void) {
  uint32_t last = lv_tick_get();
  while (1) {
    lv_timer_handler();
    print_fps();

    uint32_t elaps = lv_tick_get() - last;
    if (elaps < 5) {
      usleep((5 - elaps) * 1000);
    }
    last = lv_tick_get();
  }
}
