#include <assert.h>
#include <stdio.h>
#include "audio_block_queue.h"
#include "i2s_mirror.h"
#include "spdif_encode.h"

static void check_queue(unsigned first) {
  audio_block_queue_t q;
  const unsigned second = 3u ^ first;
  audio_block_queue_reset(&q, 3);
  assert(audio_block_queue_acquire(&q));
  int a = q.writing;
  audio_block_queue_publish(&q);
  assert(audio_block_queue_complete(&q, first) == a);
  assert(audio_block_queue_complete(&q, second) == a);
  assert(audio_block_queue_acquire(&q));
  int b = q.writing;
  assert(a != b);
  audio_block_queue_publish(&q);
  assert(audio_block_queue_complete(&q, first) == b);
  // The first consumer already reads b; the second still reads a.
  assert(!audio_block_queue_acquire(&q));
  assert(audio_block_queue_complete(&q, second) == b);
  assert(audio_block_queue_acquire(&q) && q.writing == a);
  // Encoder takes longer than DMA: both outputs must select silence.
  assert(audio_block_queue_complete(&q, first) == -1);
  audio_block_queue_publish(&q);
  assert(audio_block_queue_complete(&q, second) == -1);
  assert(audio_block_queue_complete(&q, first) == a);
  assert(audio_block_queue_complete(&q, second) == a);
  // Reset discards queued data on format changes and restart.
  audio_block_queue_reset(&q, 3);
  assert(audio_block_queue_complete(&q, second) == -1);
  assert(audio_block_queue_complete(&q, first) == -1);
  // Single SPDIF mode uses the same queue, with only one consumer.
  audio_block_queue_reset(&q, 1);
  assert(audio_block_queue_acquire(&q));
  a = q.writing;
  audio_block_queue_publish(&q);
  assert(audio_block_queue_complete(&q, 1) == a);
  assert(audio_block_queue_acquire(&q) && q.writing != a);
}

static void check_samples(unsigned depth) {
  int32_t samples[384];
  uint32_t wire[SPDIF_BLOCK_WORDS];
  uint32_t random = 1;
  for (unsigned i = 0; i < 384; ++i) {
    random = random * 1664525u + 1013904223u;
    samples[i] = (int32_t)random >> (32 - depth);
  }
  spdif_encode_block(wire, samples, depth, 48000, false);
  for (unsigned i = 0; i < 384; ++i) {
    uint64_t bits = wire[2 * i] | ((uint64_t)wire[2 * i + 1] << 32);
    uint32_t audio = 0;
    for (unsigned bit = 0; bit < 24; ++bit) {
      audio |= ((bits >> (2 * (bit + 4) + 1)) & 1) << (bit + 8);
    }
    assert(i2s_mirror_sample(samples[i], depth, false) == audio);
    assert(i2s_mirror_sample(samples[i], depth, true) == 0);
  }
}

int main(void) {
  check_queue(1);
  check_queue(2);
  spdif_encode_init();
  check_samples(16);
  check_samples(24);
  check_samples(32);
  assert(i2s_mirror_sample((int16_t)0xf872, 16, true) == 0);
  assert(i2s_mirror_sample(0x4e1f, 16, true) == 0);
  puts("PASS: paired DMA ownership and starvation; I2S matches decoded SPDIF; non-PCM muted");
}
