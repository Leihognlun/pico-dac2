#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *buffer;  // 缓冲区存储空间（动态分配）
  size_t size;      // 缓冲区长度
  size_t head;      // 读取位置索引
  size_t tail;      // 写入位置索引
  bool full;        // 缓冲区是否已满的标志
  size_t capacity;  // 实际分配的内存容量（始终满足 size <= capacity）
} ringbuffer_t;

// 初始化缓冲区（分配内存）
int ringbuffer_init(ringbuffer_t *rb, size_t size, size_t capacity);
// 清空缓冲区
void ringbuffer_clear(ringbuffer_t *rb);
// 写入数据（指定字节数）
size_t ringbuffer_write(ringbuffer_t *rb, const uint8_t *data, size_t bytes);
// 读取数据（指定字节数）
size_t ringbuffer_read(ringbuffer_t *rb, uint8_t *data, size_t bytes);
// Number of readable bytes, for checking complete audio blocks.
size_t ringbuffer_count(const ringbuffer_t *rb);
// 填充比例
float ringbuffer_fill_ratio(const ringbuffer_t *rb);

// 重新初始化（调整大小）
int ringbuffer_resize(ringbuffer_t *rb, size_t new_size);
// 释放缓冲区
void ringbuffer_free(ringbuffer_t *rb);

#ifdef __cplusplus
}
#endif
