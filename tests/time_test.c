#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "dtu_time.h"
int main(void) {
    int64_t utc=0,local=0;
    assert(dtu_time_parse("config,nettime,ok,2026,10,12,0,0,0,1\r\n",0,&utc));
    assert(utc==1791763200);
    assert(dtu_time_parse("config,nettime,ok,2026,10,12,8,0,0,1\r\n",480,&local));
    assert(utc==local);
    assert(!dtu_time_parse("config,nettime,ok,2026,2,29,0,0,0,1\r\n",0,&utc));
    assert(dtu_time_parse("config,nettime,ok,2028,2,29,0,0,0,1\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,10,12,24,0,0,1\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,10,12,0,0,0,1",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,10,12,0,0,0,1,extra\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2000,10,12,0,0,0,1\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,13,12,0,0,0,1\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,10,12,0,60,0,1\r\n",0,&utc));
    assert(!dtu_time_parse("config,nettime,ok,2026,10,12,0,0,0,1\r\n",10000,&utc));
    puts("PASS: DTU time calendar, timezone, bounds and framing.");
}
