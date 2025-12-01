#include "eye_controller.h"

#define LEFT_EYE_GIF "A:/mnt/data/panel/normal_left_eye.gif"
#define LEFT_EYELID_GIF "A:/mnt/data/panel/normal_left_eyelid.gif"
#define RIGHT_EYE_GIF "A:/mnt/data/panel/normal_right_eye.gif"
#define RIGHT_EYELID_GIF "A:/mnt/data/panel/normal_right_eyelid.gif"
#define DOG_GIF "A:/mnt/data/panel/dog.gif"
#define CAT_GIF "A:/mnt/data/panel/cat.gif"

int main(void) {
  struct eye_t left_eye, right_eye;
  eye_controller_init(&left_eye, &right_eye, LEFT_EYE_GIF, LEFT_EYELID_GIF,
                      RIGHT_EYE_GIF, RIGHT_EYELID_GIF, 28);

  // // 传入素材的路径，max_offset_px是限制的最大的偏移像素
  // eye_switch_material(&left_eye, CAT_GIF, NULL, 28);
  // eye_switch_material(&right_eye, DOG_GIF, NULL, 28);

  // eye_look_at(&left_eye, 22, -16);  // 双眼往右上看
  // eye_look_at(&right_eye, 22, -16);

  // // 设置眨眼频率：每2.2秒眨眼一次，连续眨眼2次
  // eye_blink(&left_eye, 2200, 2);
  // eye_blink(&right_eye, 2200, 2);

  // // 或者设置为持续眨眼（无限次数）
  // eye_blink(&left_eye, 3000, -1);  // 每3秒眨眼一次，无限循环
  // eye_blink(&right_eye, 3000, -1);

  // 渲染循环
  eye_controller_task();
  return 0;
}