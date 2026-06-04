#pragma once
#include <stdbool.h>

/**
 * @brief Khởi tạo GPIO cho nút bấm (pull-down, HIGH khi bấm).
 */
void button_init(void);

/**
 * @brief Kiểm tra nút có đang được bấm không (đã debounce).
 * @return true nếu đang bấm
 */
bool button_is_pressed(void);

/**
 * @brief Kiểm tra và tiêu thụ sự kiện "vừa bấm" (edge detect).
 *        Trả về true 1 lần duy nhất cho mỗi lần bấm.
 */
bool button_was_pressed(void);
