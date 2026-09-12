#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

// Two payload slots shared by one or two DMA consumers. Call under IRQ
// exclusion on the producer; the consumer calls from the DMA interrupt.
typedef struct {
  volatile int playing, next, queued, writing;
  volatile unsigned completed;
  unsigned consumers;
} audio_block_queue_t;

static inline void audio_block_queue_reset(audio_block_queue_t *q,
                                           unsigned consumers) {
  q->playing = q->next = q->queued = q->writing = -1;
  q->completed = 0;
  q->consumers = consumers;
}

static inline bool audio_block_queue_acquire(audio_block_queue_t *q) {
  if (q->queued >= 0 || q->writing >= 0) return false;
  for (int i = 0; i < 2; ++i) {
    if (i != q->playing && (q->completed == 0 || i != q->next)) {
      q->writing = i;
      return true;
    }
  }
  return false;
}

static inline void audio_block_queue_publish(audio_block_queue_t *q) {
  assert(q->writing >= 0 && q->queued < 0);
  q->queued = q->writing;
  q->writing = -1;
}

static inline int audio_block_queue_complete(audio_block_queue_t *q,
                                             unsigned consumer) {
  assert((q->consumers & consumer) && !(q->completed & consumer));
  if (q->completed == 0) {
    // The first DMA to finish decides the next block for BOTH outputs.
    q->next = q->queued;
    q->queued = -1;
  }
  int next = q->next;
  q->completed |= consumer;
  if (q->completed == q->consumers) {
    // Old storage is free only when every DMA has consumed its last word.
    q->playing = next;
    q->next = -1;
    q->completed = 0;
  }
  return next; // -1 selects silence for every consumer.
}
