#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "location_mail.h"
int main(void) {
    char body[769];
    location_result_t r={.source=LOCATION_LBS,.status=LOCATION_VALID,.longitude=113.1,.latitude=22.1};
    assert(location_mail_body(body,sizeof(body),"Synthetic test",&r,1791700000));
    assert(strstr(body,"113.1000000") && strstr(body,"22.1000000") && strstr(body,"WGS84"));
    assert(strstr(body,"LBS") && strstr(body,"1791700000"));
    r.status=LOCATION_NO_FIX;
    assert(location_mail_body(body,sizeof(body),"Synthetic test",&r,1791700000));
    assert(strstr(body,"NO_FIX") && !strstr(body,"113.1000000"));
    for (int status=LOCATION_BAD_REPLY;status<=LOCATION_IO_ERROR;status++) {
        r.status=(location_status_t)status;
        assert(location_mail_body(body,sizeof(body),"",&r,1791700000));
        assert(strstr(body,location_status_name(r.status)) && !strstr(body,"113.1000000"));
    }
    r.status=LOCATION_VALID;r.longitude=0;r.latitude=0;
    assert(!location_mail_body(body,sizeof(body),"test",&r,1791700000));
    r.longitude=NAN;assert(!location_mail_body(body,sizeof(body),"test",&r,1791700000));
    r.longitude=181;assert(!location_mail_body(body,sizeof(body),"test",&r,1791700000));
    r.longitude=113;r.latitude=91;assert(!location_mail_body(body,sizeof(body),"test",&r,1791700000));
    r.source=LOCATION_GNSS;r.latitude=22;
    assert(location_mail_body(body,sizeof(body),"test",&r,1791700000) && strstr(body,"GNSS"));
    assert(!location_mail_body(body,20,"test",&r,1791700000));
    char max_body[402];memset(max_body,'x',400);max_body[400]=0;
    assert(location_mail_body(body,sizeof(body),max_body,&r,1791700000));
    assert(strlen(body)<=768);
    max_body[400]='x';max_body[401]=0;
    assert(!location_mail_body(body,sizeof(body),max_body,&r,1791700000));
    assert(!location_mail_body(body,sizeof(body),NULL,&r,1791700000));
    assert(!location_mail_body(body,sizeof(body),"test",NULL,1791700000));
    puts("PASS: location mail valid/unavailable/invalid/bounds (synthetic data).");
}
