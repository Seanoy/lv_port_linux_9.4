#include "eye_controller.h"

#define LEFT_EYE_GIF "A:/mnt/data/panel/leye_tired.gif"
#define LEFT_EYELID_GIF "A:/mnt/data/panel/leyelid_tired.gif"
#define RIGHT_EYE_GIF "A:/mnt/data/panel/reye_tired.gif"
#define RIGHT_EYELID_GIF "A:/mnt/data/panel/reyelid_tired.gif"
#define NEW_LEFT_EYE_GIF "A:/mnt/data/panel/leye_proud.gif"
#define NEW_LEFT_EYELID_GIF "A:/mnt/data/panel/leyelid_proud.gif"
#define NEW_RIGHT_EYE_GIF "A:/mnt/data/panel/reye_proud.gif"
#define NEW_RIGHT_EYELID_GIF "A:/mnt/data/panel/reyelid_proud.gif"

int main(void) {
  struct eye_t left_eye, right_eye;
  eye_controller_init(&left_eye, &right_eye, LEFT_EYE_GIF, LEFT_EYELID_GIF,
                      LV_DISPLAY_ROTATION_270, RIGHT_EYE_GIF, RIGHT_EYELID_GIF,
                      LV_DISPLAY_ROTATION_90, 28);

  // // 传入素材的路径，max_offset_px是限制的最大的偏移像素
  // eye_switch_material(&left_eye, &right_eye, NEW_LEFT_EYE_GIF, NEW_LEFT_EYELID_GIF, 28,
  //                     NEW_RIGHT_EYE_GIF, NEW_RIGHT_EYELID_GIF, 28);

  // eye_look_at(&left_eye, 22, -16);  // 双眼往右上看
  // eye_look_at(&right_eye, 22, -16);

  // left_eyelid_blink_once();
  // right_eyelid_blink_once();

  // // 设置眨眼频率：每2.2秒眨眼一次，连续眨眼2次
  // eyelid_blink(20000, 10);

  // // 或者设置为持续眨眼（无限次数）
  // eyelid_blink(3000, -1);  // 每3秒眨眼一次，无限循环

  // 渲染循环
  eye_controller_task();
  return 0;
}