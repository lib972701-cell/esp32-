#pragma once
#include "key0_filter.h"
typedef struct { key0_filter_t keys[3]; } board_keys_filter_t;
static inline void board_keys_filter_init(board_keys_filter_t *s) {
    for(unsigned i=0;i<3;i++)key0_filter_init(&s->keys[i],30,2000);
}
/* XL9555输入端口1：KEY0/1/2=P1_7/6/5，低有效。
 * 多键同时按下或I2C读失败后，确认释放才重新武装。 */
static inline void board_keys_filter_update(board_keys_filter_t *s,bool valid,uint8_t raw,
                                             uint64_t now,key0_event_t events[3]) {
    unsigned pressed=0;
    for(unsigned key=0;key<3;key++)if(!(raw&(0x80>>key)))++pressed;
    for(unsigned key=0;key<3;key++)events[key]=key0_filter_update(&s->keys[key],
        valid && pressed<=1,(raw&(0x80>>key))==0,now);
}
