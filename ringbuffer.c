#include "ringbuffer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

int ringbuffer_init(ringbuffer_t *rb, size_t size, size_t capacity) {
  assert(rb != NULL);
  assert(0 < size);
  assert(size <= capacity);

  rb->buffer = (uint8_t *)malloc(capacity);
  if (!rb->buffer) return -1;
  rb->size = size;
  rb->capacity = capacity;
  rb->head = 0;
  rb->tail = 0;
  rb->full = false;
  return 0;
}

void ringbuffer_clear(ringbuffer_t *rb) {
  assert(rb != NULL);

  rb->head = 0;
  rb->tail = 0;
  rb->full = false;
}

size_t ringbuffer_count(const ringbuffer_t *rb) {
  assert(rb != NULL);

  if (rb->full) return rb->size;
  if (rb->head <= rb->tail) return rb->tail - rb->head;
  return rb->size - rb->head + rb->tail;
}

size_t ringbuffer_write(ringbuffer_t *rb, const uint8_t *data, size_t bytes) {
  assert(rb != NULL);
  assert(data != NULL);

  if (bytes == 0 || rb->full) return 0;
  size_t space = rb->size - ringbuffer_count(rb);
  if (space == 0) return 0;
  if (space < bytes) bytes = space;
  size_t right = rb->size - rb->tail;
  if (bytes <= right) {
    memcpy(rb->buffer + rb->tail, data, bytes);
    rb->tail += bytes;
    if (rb->tail == rb->size) rb->tail = 0;
  } else {
    memcpy(rb->buffer + rb->tail, data, right);
    memcpy(rb->buffer, data + right, bytes - right);
    rb->tail = bytes - right;
  }
  if (rb->tail == rb->head) rb->full = true;
  return bytes;
}

size_t ringbuffer_read(ringbuffer_t *rb, uint8_t *data, size_t bytes) {
  assert(rb != NULL);
  assert(data != NULL);

  if (bytes == 0) return 0;
  size_t count = ringbuffer_count(rb);
  if (count == 0) return 0;
  if (count < bytes) bytes = count;
  size_t right = rb->size - rb->head;
  if (bytes <= right) {
    memcpy(data, rb->buffer + rb->head, bytes);
    rb->head += bytes;
    if (rb->head == rb->size) rb->head = 0;
  } else {
    memcpy(data, rb->buffer + rb->head, right);
    memcpy(data + right, rb->buffer, bytes - right);
    rb->head = bytes - right;
  }
  rb->full = false;
  return bytes;
}

float ringbuffer_fill_ratio(const ringbuffer_t *rb) {
  assert(rb != NULL);
  assert(rb->size > 0);

  return ringbuffer_count(rb) / (float)rb->size;
}

// Note: resize() clears all data in the buffer
int ringbuffer_resize(ringbuffer_t *rb, size_t new_size) {
  assert(rb != NULL);
  assert(new_size > 0);

  if (new_size == rb->size) return 0;
  if (rb->capacity < new_size) {
    uint8_t *new_buf = (uint8_t *)realloc(rb->buffer, new_size);
    if (!new_buf) return -1;
    rb->buffer = new_buf;
    rb->capacity = new_size;
  }
  rb->size = new_size;
  rb->head = 0;
  rb->tail = 0;
  rb->full = false;
  return 0;
}

void ringbuffer_free(ringbuffer_t *rb) {
  assert(rb != NULL);

  free(rb->buffer);
  rb->buffer = NULL;
  rb->size = 0;
  rb->capacity = 0;
  rb->head = 0;
  rb->tail = 0;
  rb->full = false;
}
