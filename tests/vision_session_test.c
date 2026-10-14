#include "vision_session.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { char tx[160]; unsigned writes, starts, stops; bool cloud_running; } fake_t;
static bool send_wire(void *ctx, const char *data, size_t size) {
    fake_t *f=ctx; assert(size<sizeof(f->tx));
    memcpy(f->tx,data,size); f->tx[size]=0; ++f->writes; return true;
}
static bool cloud_control(void *ctx, bool start) {
    fake_t *f=ctx;
    if(start) { ++f->starts; f->cloud_running=true; } else ++f->stops;
    return true; /* 请求不等于停止完成，tick的cloud_stopped才是确认。 */
}
static void init(vision_session_t *s, fake_t *f, bool configured) {
    memset(f,0,sizeof(*f)); vision_session_init(s,configured,send_wire,cloud_control,f);
}
static void feed(vision_session_t *s,const char *body,uint64_t now) {
    char wire[160]; size_t n=vision_encode(body,wire,sizeof(wire)); assert(n);
    vision_session_feed(s,(const uint8_t *)wire,n,now);
}
static void hello(vision_session_t *s, uint64_t now) { feed(s,"HELLO,1,boot-a",now); }
static void ack(vision_session_t *s,const char *mode,uint32_t epoch,uint64_t now) {
    char body[80]; snprintf(body,sizeof(body),"ACK,%lu,%lu,%s",
        (unsigned long)s->pending_seq,(unsigned long)epoch,mode); feed(s,body,now);
}
static void idle(vision_session_t *s,fake_t *f) {
    vision_session_tick(s,0,true); assert(strncmp(f->tx,"SYNC*",5)==0);
    hello(s,1); vision_session_tick(s,2,true); ack(s,"IDLE",1,3);
}
static void light(vision_session_t *s,fake_t *f) {
    idle(s,f); vision_session_request(s,ACTIVITY_LIGHT);
    vision_session_tick(s,4,true); ack(s,"LIGHT",2,5);
    feed(s,"RES,1,2,LIGHT,green,0,0,0,0",6);
}
static void test_cloud_waits_for_remote_idle_ack(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); light(&s,&f);
    assert(s.result.valid);
    vision_session_request(&s,ACTIVITY_CLOUD); assert(!s.result.valid);
    vision_session_tick(&s,10,true); assert(f.starts==0);
    assert(strstr(f.tx,",IDLE*"));
    feed(&s,"ACK,1,1,IDLE",11); vision_session_tick(&s,12,true); assert(f.starts==0);
    ack(&s,"IDLE",3,13); vision_session_tick(&s,14,true); assert(f.starts==1);
    assert(!s.result.valid);
}
static void test_vision_waits_for_cloud_stop(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); idle(&s,&f);
    vision_session_request(&s,ACTIVITY_CLOUD); vision_session_tick(&s,4,true); assert(f.starts==1);
    unsigned writes=f.writes;
    vision_session_request(&s,ACTIVITY_PERSON); vision_session_tick(&s,5,false);
    assert(f.stops==1); assert(f.writes==writes); assert(!s.result.valid);
    vision_session_tick(&s,6,false); assert(f.stops==1);
    vision_session_tick(&s,7,true); assert(strstr(f.tx,",PERSON*"));
    ack(&s,"PERSON",2,8); feed(&s,"RES,2,2,PERSON,person,2,1,0,1",9);
    assert(s.result.valid && s.result.count==2 && s.result.right==1);
}
static void test_latest_intent_and_long_keys(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); idle(&s,&f);
    vision_session_key(&s,1,false); vision_session_tick(&s,4,true);
    uint32_t old=s.pending_seq;
    vision_session_key(&s,0,false); vision_session_key(&s,2,false);
    vision_session_tick(&s,5,true); assert(strstr(f.tx,",PERSON*"));
    char body[80]; snprintf(body,sizeof(body),"ACK,%lu,2,LIGHT",(unsigned long)old);
    feed(&s,body,6); assert(s.pending);
    ack(&s,"PERSON",3,7);
    vision_session_key(&s,1,true); assert(s.desired==ACTIVITY_PERSON);
    vision_session_key(&s,0,true); assert(s.desired==ACTIVITY_PERSON);
    vision_session_key(&s,2,true); assert(s.desired==ACTIVITY_OFF);
    vision_session_tick(&s,8,true); assert(strstr(f.tx,",IDLE*")); assert(f.starts==0);
}
static void test_unconfigured_preserves_key0(void) {
    vision_session_t s; fake_t f; init(&s,&f,false);
    vision_session_key(&s,0,false); vision_session_tick(&s,0,true);
    assert(f.starts==1 && f.writes==0);
    vision_session_key(&s,1,false); vision_session_tick(&s,1,false);
    assert(f.stops==1 && f.writes==0 && s.phase==VISION_UNCONFIGURED);
    vision_session_tick(&s,2,true); assert(!s.result.valid);
    vision_session_key(&s,1,true); vision_session_key(&s,0,false);
    vision_session_tick(&s,3,true); assert(f.starts==2);
}
static void test_crc_fragments_and_bad_fields(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); light(&s,&f);
    assert(vision_crc16((const uint8_t *)"123456789",9)==0x29B1);
    char wire[160]; size_t n=vision_encode("RES,2,2,LIGHT,red,0,0,0,0",wire,sizeof(wire));
    vision_session_feed(&s,(uint8_t *)wire,8,7); assert(!strcmp(s.result.state,"green"));
    vision_session_feed(&s,(uint8_t *)wire+8,n-8,8); assert(!strcmp(s.result.state,"red"));
    wire[5]='9'; vision_session_feed(&s,(uint8_t *)wire,n,9); assert(s.result.frame==2);
    feed(&s,"RES,3,2,LIGHT,green,1,0,0,0",10); assert(s.result.frame==2);
    feed(&s,"RES,3,2,LIGHT,green,,0,0,0",10); assert(s.result.frame==2);
    feed(&s,"RES,3,2,LIGHT,green,0,0,0,0,extra",10); assert(s.result.frame==2);
    feed(&s,"RES,3,1,LIGHT,green,0,0,0,0",10); assert(s.result.frame==2);
    feed(&s,"RES,2,2,LIGHT,green,0,0,0,0",10); assert(s.result.frame==2);
    feed(&s,"RES,+3,2,LIGHT,green,0,0,0,0",10); assert(s.result.frame==2);
    feed(&s,"RES,4294967296,2,LIGHT,green,0,0,0,0",10); assert(s.result.frame==2);
    char huge[180]; memset(huge,'A',sizeof(huge)); huge[179]='\n';
    vision_session_feed(&s,(uint8_t *)huge,sizeof(huge),11);
    feed(&s,"RES,3,2,LIGHT,green,0,0,0,0",12); assert(s.result.frame==3);
    n=vision_encode("RES,4,2,LIGHT,red,0,0,0,0",wire,sizeof(wire));
    vision_session_feed(&s,(uint8_t *)wire,7,20);
    vision_session_feed(&s,(uint8_t *)wire+7,n-7,600); assert(s.result.frame==3);
    feed(&s,"RES,4,2,LIGHT,red,0,0,0,0",601); assert(s.result.frame==4);
    assert(vision_encode("SYNC\n",wire,sizeof(wire))==0);
}
static void test_disconnect_blocks_cloud_and_restart_cancels(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); light(&s,&f);
    vision_session_tick(&s,1010,true); assert(!s.result.valid && !s.synchronized);
    vision_session_request(&s,ACTIVITY_CLOUD); vision_session_tick(&s,1011,true); assert(f.starts==0);
    hello(&s,1012); vision_session_tick(&s,1013,true); assert(f.starts==0);
    ack(&s,"IDLE",4,1014); vision_session_tick(&s,1015,true); assert(f.starts==1);
    feed(&s,"HELLO,1,boot-b",1016); vision_session_tick(&s,1017,false);
    assert(f.stops==1 && s.desired==ACTIVITY_OFF);
}
static void test_ack_timeout_retries_and_fault(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); idle(&s,&f);
    vision_session_request(&s,ACTIVITY_LIGHT); vision_session_tick(&s,10,true);
    uint32_t seq=s.pending_seq; unsigned writes=f.writes;
    vision_session_tick(&s,20009,true); assert(f.writes==writes);
    vision_session_tick(&s,20010,true); assert(s.pending_seq==seq && f.writes==writes+1);
    vision_session_tick(&s,40010,true); assert(s.pending_seq==seq && f.writes==writes+2);
    vision_session_tick(&s,60010,true); assert(!s.synchronized && s.desired==ACTIVITY_OFF);
    assert(f.starts==0);
    init(&s,&f,true); light(&s,&f); feed(&s,"FAULT,3,IDLE",7);
    assert(!s.result.valid && s.desired==ACTIVITY_OFF);
}
static void test_person_semantics_and_wrap(void) {
    vision_session_t s; fake_t f; init(&s,&f,true); idle(&s,&f);
    vision_session_request(&s,ACTIVITY_PERSON); vision_session_tick(&s,4,true); ack(&s,"PERSON",2,5);
    feed(&s,"RES,2147483647,2,PERSON,person,999,999,1,0",6); assert(s.result.valid);
    feed(&s,"RES,0,2,PERSON,none,0,0,0,0",7); assert(s.result.frame==0 && !strcmp(s.result.state,"none"));
    feed(&s,"RES,1,2,PERSON,none,1,1,0,0",8); assert(s.result.frame==0);
    feed(&s,"RES,1,2,PERSON,person,2,1,0,0",8); assert(s.result.frame==0);
    feed(&s,"RES,1,2,PERSON,unknown,0,0,0,0",8); assert(s.result.frame==1);
}
static void test_cloud_failure_needs_new_short_press(void) {
    vision_session_t s; fake_t f; init(&s,&f,false);
    vision_session_key(&s,0,false); vision_session_tick(&s,0,true); assert(f.starts==1);
    vision_session_tick(&s,1,false);
    vision_session_tick(&s,2,true); /* 网络失败已完成关闭，不自动重试对话。 */
    assert(s.desired==ACTIVITY_OFF && !s.cloud_requested && f.starts==1);
    vision_session_key(&s,0,false); vision_session_tick(&s,3,true); assert(f.starts==2);
}
int main(void) {
    test_cloud_waits_for_remote_idle_ack(); test_vision_waits_for_cloud_stop();
    test_latest_intent_and_long_keys(); test_unconfigured_preserves_key0();
    test_crc_fragments_and_bad_fields(); test_disconnect_blocks_cloud_and_restart_cancels();
    test_ack_timeout_retries_and_fault(); test_person_semantics_and_wrap();
    test_cloud_failure_needs_new_short_press();
    puts("vision_session: 9 scenario groups passed"); return 0;
}
