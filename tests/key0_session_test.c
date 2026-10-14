#include <assert.h>
#include <stdio.h>
#include "key0_filter.h"
#include "conversation_gate.h"
#include "board_keys_filter.h"

static void test_expander_keys(void) {
    board_keys_filter_t keys; key0_event_t events[3];
    board_keys_filter_init(&keys);
    board_keys_filter_update(&keys,true,0xff,0,events);
    board_keys_filter_update(&keys,true,0xff,30,events);
    board_keys_filter_update(&keys,true,0xbf,100,events); /* KEY1=P1_6 */
    board_keys_filter_update(&keys,true,0xbf,130,events);
    board_keys_filter_update(&keys,true,0xff,200,events);
    board_keys_filter_update(&keys,true,0xff,230,events);
    assert(events[0]==KEY0_NONE && events[1]==KEY0_SHORT && events[2]==KEY0_NONE);
    board_keys_filter_update(&keys,true,0xdf,300,events); /* KEY2=P1_5 */
    board_keys_filter_update(&keys,true,0xdf,330,events);
    board_keys_filter_update(&keys,true,0xdf,2330,events);
    assert(events[2]==KEY0_LONG && events[0]==KEY0_NONE && events[1]==KEY0_NONE);
    board_keys_filter_update(&keys,true,0xff,2400,events);
    board_keys_filter_update(&keys,true,0xff,2430,events);
    assert(events[2]==KEY0_NONE);
    board_keys_filter_update(&keys,true,0xbf,2500,events);
    board_keys_filter_update(&keys,true,0xbf,2530,events);
    board_keys_filter_update(&keys,true,0x9f,2540,events); /* KEY1+KEY2同时按下 */
    board_keys_filter_update(&keys,true,0xbf,2600,events);
    board_keys_filter_update(&keys,true,0xff,2630,events);
    board_keys_filter_update(&keys,true,0xff,2660,events);
    assert(events[0]==KEY0_NONE && events[1]==KEY0_NONE && events[2]==KEY0_NONE);
    board_keys_filter_update(&keys,true,0x7f,2700,events);
    board_keys_filter_update(&keys,true,0x7f,2730,events);
    board_keys_filter_update(&keys,false,0xff,2800,events);
    board_keys_filter_update(&keys,true,0xff,2830,events);
    board_keys_filter_update(&keys,true,0xff,2860,events);
    assert(events[0]==KEY0_NONE); /* 读失败不伪造释放事件 */
}

int main(void) {
    test_expander_keys();
    key0_filter_t key;
    key0_filter_init(&key, 30, 2000);
    assert(key0_filter_update(&key, true, false, 0) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 10) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 20) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 60) == KEY0_NONE);
    /* 短按只在消抖后的释放触发。 */
    assert(key0_filter_update(&key, true, true, 100) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 130) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 200) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 230) == KEY0_SHORT);
    assert(key0_filter_update(&key, true, false, 240) == KEY0_NONE);
    /* 长按只触发一次，松手不启动。 */
    assert(key0_filter_update(&key, true, true, 300) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 330) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 2329) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 2330) == KEY0_LONG);
    assert(key0_filter_update(&key, true, true, 2400) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 2410) == KEY0_NONE);
    assert(key0_filter_update(&key, true, false, 2440) == KEY0_NONE);
    /* I2C 失败不当作按下/释放；恢复后必须先确认释放。 */
    key0_filter_update(&key, true, true, 2500);
    key0_filter_update(&key, true, true, 2530);
    assert(key0_filter_update(&key, false, false, 2600) == KEY0_NONE);
    assert(key0_filter_update(&key, true, true, 6000) == KEY0_NONE);
    key0_filter_update(&key, true, false, 6010);
    assert(key0_filter_update(&key, true, false, 6040) == KEY0_NONE);
    key0_filter_update(&key, true, true, 6100);
    key0_filter_update(&key, true, true, 6130);
    key0_filter_update(&key, true, false, 6200);
    assert(key0_filter_update(&key, true, false, 6230) == KEY0_SHORT);
    /* 上电按住不产生启动或长按，先松手。 */
    key0_filter_init(&key, 30, 2000);
    key0_filter_update(&key, true, true, 0);
    assert(key0_filter_update(&key, true, true, 3000) == KEY0_NONE);
    key0_filter_update(&key, true, false, 3010);
    assert(key0_filter_update(&key, true, false, 3040) == KEY0_NONE);

    conversation_gate_t gate = {0};
    assert(conversation_gate_stopped(&gate));
    uint32_t first = conversation_gate_start(&gate);
    assert(conversation_gate_current(&gate, first));
    assert(!conversation_gate_stopped(&gate));
    assert(conversation_gate_start(&gate) == first);
    conversation_gate_stop(&gate);
    assert(!conversation_gate_current(&gate, first));
    assert(!conversation_gate_stopped(&gate)); /* 停止意图还不是硬件/通道关闭确认。 */
    conversation_gate_closed(&gate);
    assert(conversation_gate_stopped(&gate));
    uint32_t next = conversation_gate_start(&gate);
    assert(next != first);
    assert(!conversation_gate_current(&gate, first));
    assert(conversation_gate_current(&gate, next));
    conversation_gate_closed(&gate); /* 旧关闭回调不能确认新会话已停止。 */
    assert(!conversation_gate_stopped(&gate));
    conversation_gate_stop(&gate);
    assert(!gate.requested);
    conversation_gate_closed(&gate);
    assert(conversation_gate_stopped(&gate));
    puts("KEY0 debounce, long press, read failure and cancelled session tests passed");
    return 0;
}
