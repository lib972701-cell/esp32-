#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "location_protocol.h"
#include "dtu_frame.h"
int main(void) {
    location_result_t r;
    assert(!dtu_frame_byte_valid(0));
    assert(!dtu_frame_byte_valid(1));
    assert(!dtu_frame_byte_valid(127));
    assert(dtu_frame_byte_valid('\r') && dtu_frame_byte_valid('\n'));
    for (unsigned ch=32;ch<127;ch++) assert(dtu_frame_byte_valid((unsigned char)ch));
    assert(location_parse("config,lbsloc,ok,113.1234567,22.1234567\r\n",LOCATION_LBS,&r)==LOCATION_VALID);
    assert(fabs(r.longitude-113.1234567)<1e-8 && r.source==LOCATION_LBS);
    assert(location_parse("config,lbsloc,ok,000.0000000,000.0000000\r\n",LOCATION_LBS,&r)==LOCATION_NO_FIX);
    assert(r.longitude==0 && r.latitude==0);
    assert(location_parse("config,lbsloc,ok,113.0,22.0",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,nan,22\r\n",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,1e2,22\r\n",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,181,22\r\n",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,113,91\r\n",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,113,22,extra\r\n",LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,error,1\r\n",LOCATION_LBS,&r)==LOCATION_DEVICE_ERROR && r.error_code==1);
    assert(location_parse("config,gps,ok,1,E,113.9739056,N,22.6927826,0,0,184,-3.6,0,2022-11-3 2:43:51\r\n",LOCATION_GNSS,&r)==LOCATION_VALID);
    assert(location_parse("config,gps,ok,1,W,113.9,S,22.6,0,0,0,0,0,2026-10-11 1:00:00\r\n",LOCATION_GNSS,&r)==LOCATION_VALID);
    assert(r.longitude<0 && r.latitude<0);
    assert(location_parse("config,gps,ok,0,E,113.9,N,22.6,0,0,0,0,0,old\r\n",LOCATION_GNSS,&r)==LOCATION_NO_FIX);
    assert(r.longitude==0 && r.latitude==0); /* Never expose cached coordinates without a fix. */
    assert(location_parse("config,gps,ok,1,X,113.9,N,22.6,0,0,0,0,0,time\r\n",LOCATION_GNSS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,gps,ok,1,E,11354.0,N,2236.0,0,0,0,0,0,time\r\n",LOCATION_GNSS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse("config,lbsloc,ok,113,22\r\n",LOCATION_GNSS,&r)==LOCATION_BAD_REPLY);
    assert(location_parse(NULL,LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    char long_frame[600];memset(long_frame,'a',sizeof(long_frame));long_frame[599]=0;
    assert(location_parse(long_frame,LOCATION_LBS,&r)==LOCATION_BAD_REPLY);
    puts("Location protocol: 18 scenarios passed (synthetic coordinates only).");
}
