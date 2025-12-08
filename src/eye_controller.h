#ifndef EYE_CONTROLLER_H
#define EYE_CONTROLLER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 眼睛结构体 */
struct eye_t {
  lv_disp_t *disp;       // 关联的显示器
  lv_obj_t *eye_gif;     // 眼球GIF对象
  lv_obj_t *eyelid_gif;  // 眼睑GIF对象
  int32_t max_offset;    // 最大偏移量
};

/* 眼皮控制器结构体 */
typedef struct eyelid_controller_t {
  struct eye_t *left_eye;   // 左眼指针
  struct eye_t *right_eye;  // 右眼指针
  lv_timer_t *blink_timer;  // 统一的眨眼定时器
  uint32_t blink_interval;  // 眨眼间隔
  int32_t blink_remaining;  // 剩余眨眼次数
} eyelid_controller_t;

/* 初始化眼睛控制器 */
void eye_controller_init(
    struct eye_t *left_eye, struct eye_t *right_eye, const char *left_eye_path,
    const char *left_eyelid_path, lv_display_rotation_t rotation_left,
    const char *right_eye_path, const char *right_eyelid_path,
    lv_display_rotation_t rotation_right, uint32_t max_offset_px);

/* 反初始化 */
void eye_controller_deinit(void);

/* 主任务循环 */
void eye_controller_task(void);

/* 同步控制两个眼皮 */
void eyelid_blink(uint32_t interval_ms, int32_t count);
void eyelid_blink_once(void);

/* 单独控制左眼皮 */
void left_eyelid_blink(uint32_t interval_ms, int32_t count);
void left_eyelid_blink_once(void);

/* 单独控制右眼皮 */
void right_eyelid_blink(uint32_t interval_ms, int32_t count);
void right_eyelid_blink_once(void);

/* 视线控制 */
void eye_look_at(struct eye_t *eye, int32_t tx, int32_t ty);
void left_eye_look_at(int32_t tx, int32_t ty);
void right_eye_look_at(int32_t tx, int32_t ty);

/* 素材切换 */
void eye_switch_material(struct eye_t *left_eye, struct eye_t *right_eye,
                         const char *left_eye_gif_path,
                         const char *left_eyelid_gif_path,
                         int32_t left_max_offset_px,
                         const char *right_eye_gif_path,
                         const char *right_eyelid_gif_path,
                         int32_t right_max_offset_px);

/* 销毁单个眼睛对象 */
void eye_destroy(struct eye_t *eye);

#ifdef __cplusplus
}
#endif

#endif /* EYE_CONTROLLER_H */
