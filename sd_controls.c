#include "sd_controls.h"
#include "cec_arc.h"
#include "pico/stdlib.h"
#include <stdatomic.h>

static const unsigned keys[4]={10,13,21,27};
static const unsigned leds_p[4]={9,12,20,26};
static const unsigned leds_n[4]={11,14,22,28};
static uint8_t raw,stable; static uint32_t changed[4];
static bool playing=true; static atomic_uint next_generation;

static void update_leds(void){
  for(unsigned i=0;i<4;++i){gpio_put(leds_n[i],false);gpio_put(leds_p[i],i==0||playing);}
}
void sd_controls_init(void){
  for(unsigned i=0;i<4;++i){gpio_init(keys[i]);gpio_set_dir(keys[i],GPIO_IN);gpio_pull_up(keys[i]);
    gpio_init(leds_p[i]);gpio_put(leds_p[i],false);gpio_set_dir(leds_p[i],GPIO_OUT);
    gpio_init(leds_n[i]);gpio_put(leds_n[i],false);gpio_set_dir(leds_n[i],GPIO_OUT);changed[i]=time_us_32();}
  update_leds();
}
void sd_controls_task(void){
  uint32_t now=time_us_32();
  for(unsigned i=0;i<4;++i){unsigned bit=1u<<i;bool down=!gpio_get(keys[i]);
    if(down!=!!(raw&bit)){raw^=bit;changed[i]=now;}
    if(now-changed[i]<20000||down==!!(stable&bit))continue;
    stable^=bit;
    if(i==0&&down){playing=!playing;if(playing)cec_arc_set_enabled(true);else{cec_arc_volume_key(true,false);cec_arc_volume_key(false,false);}update_leds();}
    else if(playing&&i==1&&down)
      atomic_fetch_add_explicit(&next_generation, 1, memory_order_release);
    else if(playing&&i==2)cec_arc_volume_key(true,down);
    else if(playing&&i==3)cec_arc_volume_key(false,down);
  }
}
bool sd_controls_playing(void){return playing;}
unsigned sd_controls_next_generation(void){
  return atomic_load_explicit(&next_generation, memory_order_acquire);
}
