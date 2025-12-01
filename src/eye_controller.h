#ifndef EYE_CONTROLLER_H
#define EYE_CONTROLLER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 眼睛控制器结构体 ==================== */
struct eye_t {
  lv_disp_t *disp;          // 显示的屏幕
  lv_obj_t *eye_gif;        // 眼球动图（循环播放）
  lv_obj_t *eyelid_gif;     // 眼睑动图（单次播放）
  lv_timer_t *blink_timer;  // 眨眼定时器
  int max_offset;  // 允许的最大偏移像素（根据眼白大小调整）
  int32_t blink_remaining;  // 还剩几次要眨（-1=无限）
  uint32_t blink_interval;  // 眨眼间隔（毫秒）
};
/* ==================== 函数声明 ==================== */

/**
 * @brief 初始化眼睛
 * @param left_eye 右眼结构体指针
 * @param right_eye 右眼结构体指针
 * @param left_eye_path 初始眼球GIF路径
 * @param left_eyelid_path 初始左眼睑GIF路径
 * @param right_eye_path 初始右眼球GIF路径
 * @param right_eyelid_path 初始右眼睑GIF路径
 * @param max_offset 最大偏移像素
 */
void eye_controller_init(struct eye_t *left_eye, struct eye_t *right_eye,
                         char *left_eye_path, char *left_eyelid_path,
                         char *right_eye_path, char *right_eyelid_path,
                         uint32_t max_offset_px);

/**
 * @brief 反初始化眼睛
 * @param left_eye 右眼结构体指针
 * @param right_eye 右眼结构体指针
 */
void eye_controller_deinit(struct eye_t *left_eye, struct eye_t *right_eye);

/**
 * @brief 控制眼睛看向指定位置
 * @param eye 眼睛结构体指针
 * @param tx X轴偏移量
 * @param ty Y轴偏移量
 * @note 坐标轴原点是屏幕中心 →x ↓y
 */
void eye_look_at(struct eye_t *eye, int32_t tx, int32_t ty);

/**
 * @brief 控制眨眼频率和次数
 * @param eye 眼睛结构体指针
 * @param interval_ms 眨眼间隔时间（毫秒）
 * @param count 眨眼次数（-1=无限，0=停止，>0=具体次数）
 */
void eye_blink(struct eye_t *eye, uint32_t interval_ms, int32_t count);

/**
 * @brief 立即眨眼一次
 * @param eye 眼睛结构体指针
 */
void eye_blink_once(struct eye_t *eye);

/**
 * @brief 切换眼睛素材
 * @param eye 眼睛结构体指针
 * @param eye_gif_path 新的眼球GIF路径
 * @param eyelid_gif_path 新的眼睑GIF路径
 * @param max_offset_px 最大偏移像素
 */
void eye_switch_material(struct eye_t *eye, const char *eye_gif_path,
                         const char *eyelid_gif_path, int32_t max_offset_px);

/**
 * @brief 渲染主循环
 */
void eye_controller_task(void);

#ifdef __cplusplus
}
#endif

#endif /* EYE_CONTROLLER_H */