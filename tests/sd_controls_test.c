#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "sd_controls.h"
static bool pins[30];static uint32_t now;static unsigned volume_events,system_audio_events;static bool last_up,last_down;
uint32_t time_us_32(void){return now;}void gpio_init(unsigned p){(void)p;}void gpio_set_dir(unsigned p,int d){(void)p;(void)d;}
void gpio_pull_up(unsigned p){pins[p]=true;}void gpio_put(unsigned p,int v){pins[p]=v;}int gpio_get(unsigned p){return pins[p];}
void cec_arc_volume_key(bool up,bool down){last_up=up;last_down=down;++volume_events;}
void cec_arc_request_playback(void){++system_audio_events;}
static void key(unsigned p,bool down){pins[p]=!down;sd_controls_task();now+=21000;sd_controls_task();}
int main(void){sd_controls_init();assert(pins[9]&&pins[12]&&pins[20]&&pins[26]);
 key(10,true);key(10,false);assert(!sd_controls_playing());assert(pins[9]&&!pins[12]&&!pins[20]&&!pins[26]);volume_events=0;
 unsigned n=sd_controls_next_generation();key(13,true);key(13,false);key(21,true);key(21,false);assert(sd_controls_next_generation()==n&&volume_events==0);
 key(10,true);key(10,false);assert(sd_controls_playing()&&system_audio_events==1&&sd_controls_play_generation()==1);key(13,true);key(13,false);assert(sd_controls_next_generation()==n+1);
 key(21,true);assert(last_up&&last_down);key(21,false);assert(last_up&&!last_down);key(27,true);assert(!last_up&&last_down);key(27,false);assert(!last_up&&!last_down);
 sd_controls_stop();assert(!sd_controls_playing()&&pins[9]&&!pins[12]&&!pins[20]&&!pins[26]);
 puts("PASS: B1 play/stop LEDs, CEC playback routing request, EOF stop and playback-only B2/B3/B4 controls");}
