#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdnoreturn.h>

#include "boot_image.h"

/*
 * 前置条件：
 * 1. vector_table 不能为 NULL。
 * 2. vector_table 必须已经通过 boot_image_check_vector_table() 检查。
 * 3. 函数成功执行后不会返回。
 */
noreturn void boot_jump_to_application(const boot_image_vector_table_t *vector_table);

#endif /* BOOT_JUMP_H */