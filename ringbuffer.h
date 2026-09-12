#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t *buffer;  // バッファ本体（動的確保）
  size_t size;      // バッファ長
  size_t head;      // 書き込み位置インデックス
  size_t tail;      // 読み出し位置インデックス
  bool full;        // バッファが満杯かどうかのフラグ
  size_t capacity;  // 実際に確保しているメモリ長 (常に size <= capacity)
} ringbuffer_t;

// バッファを初期化（メモリ確保）
int ringbuffer_init(ringbuffer_t *rb, size_t size, size_t capacity);
// バッファをクリア
void ringbuffer_clear(ringbuffer_t *rb);
// 書き込み（バイト数指定）
size_t ringbuffer_write(ringbuffer_t *rb, const uint8_t *data, size_t bytes);
// 読み込み（バイト数指定）
size_t ringbuffer_read(ringbuffer_t *rb, uint8_t *data, size_t bytes);
// Number of readable bytes, for checking complete audio blocks.
size_t ringbuffer_count(const ringbuffer_t *rb);
// 充填率
float ringbuffer_fill_ratio(const ringbuffer_t *rb);

// 再初期化（サイズ変更）
int ringbuffer_resize(ringbuffer_t *rb, size_t new_size);
// バッファ解放
void ringbuffer_free(ringbuffer_t *rb);

#ifdef __cplusplus
}
#endif
