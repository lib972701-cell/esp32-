#include "vision_session.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>

const char *activity_mode_name(activity_mode_t mode) {
    static const char *const names[]={"IDLE","CLOUD","LIGHT","PERSON"};
    return (unsigned)mode<4 ? names[mode] : "INVALID";
}
const char *vision_phase_name(vision_phase_t phase) {
    static const char *const names[]={"IDLE","SYNCING","SWITCHING","WAIT_CLOUD_STOP",
                                      "VISION_ACTIVE","CLOUD_REQUESTED","UART_UNCONFIGURED"};
    return (unsigned)phase<7 ? names[phase] : "INVALID";
}
uint16_t vision_crc16(const uint8_t *data,size_t n) {
    uint16_t crc=0xffff;
    while(n--) {
        crc^=(uint16_t)*data++<<8;
        for(unsigned i=0;i<8;i++)crc=(uint16_t)((crc&0x8000) ? (crc<<1)^0x1021 : crc<<1);
    }
    return crc;
}
size_t vision_encode(const char *body,char *wire,size_t capacity) {
    if(!body||!wire)return 0;
    size_t n=strlen(body);
    if(!n || n>154 || capacity<n+7)return 0;
    for(size_t i=0;i<n;i++)if((unsigned char)body[i]<32 || (unsigned char)body[i]>126 || body[i]=='*')return 0;
    memcpy(wire,body,n);
    snprintf(wire+n,capacity-n,"*%04X\n",vision_crc16((const uint8_t *)body,n));
    return n+6;
}
void vision_session_init(vision_session_t *s,bool configured,vision_send_fn send,vision_cloud_fn cloud,void *ctx) {
    memset(s,0,sizeof(*s)); s->configured=configured; s->send=send; s->cloud=cloud; s->context=ctx;
    strcpy(s->result.state,"unknown");
}
void vision_session_request(vision_session_t *s,activity_mode_t mode) {
    if((unsigned)mode>ACTIVITY_PERSON)return;
    if(s->desired==mode)return;
    s->desired=mode; s->result.valid=false; s->last_error[0]=0;
}
void vision_session_key(vision_session_t *s,unsigned key,bool long_press) {
    if(key>2)return;
    activity_mode_t mode=(activity_mode_t)(ACTIVITY_CLOUD+key);
    if(!long_press)vision_session_request(s,mode);
    else if(s->desired==mode)vision_session_request(s,ACTIVITY_OFF);
}
void vision_session_fault(vision_session_t *s,const char *reason) {
    s->desired=ACTIVITY_OFF; s->result.valid=false;
    s->synchronized=s->pending=s->ack_valid=s->frame_valid=false;
    s->sync_sent=false;
    snprintf(s->last_error,sizeof(s->last_error),"%s",reason);
}
static bool send_body(vision_session_t *s,const char *body) {
    char wire[161]; size_t n=vision_encode(body,wire,sizeof(wire));
    if(!n || !s->send || !s->send(s->context,wire,n)) {
        vision_session_fault(s,"UART_WRITE_FAILED"); return false;
    }
    return true;
}
static void transmit(vision_session_t *s,uint64_t now) {
    char body[64]; snprintf(body,sizeof(body),"MODE,%lu,%s",
        (unsigned long)s->pending_seq,activity_mode_name(s->pending_mode));
    if(send_body(s,body)) { s->sent_ms=now; ++s->attempts; }
}
static bool number(const char *text,uint32_t *out) {
    if(!*text)return false;
    uint32_t value=0;
    for(const char *p=text;*p;p++) {
        if(*p<'0'||*p>'9')return false;
        unsigned digit=(unsigned)(*p-'0');
        if(value>(UINT32_MAX-digit)/10)return false;
        value=value*10+digit;
    }
    *out=value; return true;
}
static bool remote_mode(const char *text,activity_mode_t *mode) {
    if(!strcmp(text,"IDLE"))*mode=ACTIVITY_OFF;
    else if(!strcmp(text,"LIGHT"))*mode=ACTIVITY_LIGHT;
    else if(!strcmp(text,"PERSON"))*mode=ACTIVITY_PERSON;
    else return false;
    return true;
}
static void handle(vision_session_t *s,char *body,uint64_t now) {
    char *f[10]; unsigned n=0; f[n++]=body;
    for(char *p=body;*p;p++)if(*p==',') {
        if(n==10) { ++s->protocol_errors; return; }
        *p=0; f[n++]=p+1;
    }
    for(unsigned i=0;i<n;i++)if(!*f[i]) { ++s->protocol_errors; return; }
    uint32_t seq,epoch;
    activity_mode_t mode;
    if(n==3 && !strcmp(f[0],"HELLO") && !strcmp(f[1],"1")) {
        /* 未请求的HELLO意味着远端重启/复位；取消旧业务，绝不保留旧绿灯。 */
        if(s->synchronized)vision_session_fault(s,"REMOTE_RESTART");
        s->synchronized=true; s->pending=s->ack_valid=s->frame_valid=false;
        s->sequence=0; s->result.valid=false; return;
    }
    if(n==4 && !strcmp(f[0],"ACK") && number(f[1],&seq) && number(f[2],&epoch) && remote_mode(f[3],&mode)) {
        if(!s->pending || seq!=s->pending_seq || mode!=s->pending_mode)return;
        s->epoch=epoch; s->remote_mode=mode; s->pending=false; s->ack_valid=true;
        s->frame_valid=false; s->last_result_ms=now; s->result.valid=false; return;
    }
    if(n==5 && !strcmp(f[0],"ERR") && number(f[1],&seq) && number(f[3],&epoch) && remote_mode(f[4],&mode)) {
        if(s->pending && seq==s->pending_seq)vision_session_fault(s,f[2]);
        return;
    }
    if(n==3 && !strcmp(f[0],"FAULT") && number(f[1],&epoch) && !strcmp(f[2],"IDLE")) {
        vision_session_fault(s,"REMOTE_FAULT"); return;
    }
    if(n==2 && !strcmp(f[0],"BUSY") && number(f[1],&epoch))return;
    if(n!=9 || strcmp(f[0],"RES") || !s->ack_valid || s->pending ||
       !number(f[1],&seq) || seq>INT32_MAX || !number(f[2],&epoch) || epoch!=s->epoch ||
       !remote_mode(f[3],&mode) || mode!=s->remote_mode)return;
    uint32_t values[4];
    for(unsigned i=0;i<4;i++)if(!number(f[5+i],&values[i]) || values[i]>999)return;
    uint32_t total=values[1]+values[2]+values[3]; if(total>999)total=999;
    bool valid_state=false;
    if(mode==ACTIVITY_PERSON) {
        valid_state=(!strcmp(f[4],"person") && values[0]>0 && values[0]==total) ||
                    ((!strcmp(f[4],"none") || !strcmp(f[4],"unknown")) && values[0]==0 && total==0);
    } else if(values[0]==0 && total==0) {
        valid_state=!strcmp(f[4],"unknown") || (mode==ACTIVITY_LIGHT &&
                    (!strcmp(f[4],"red") || !strcmp(f[4],"green") || !strcmp(f[4],"ambiguous")));
    }
    if(!valid_state)return;
    /* Maix frame模2^31；只接收向前推进的帧，兼容长期运行的回绕。 */
    uint32_t delta=(seq-s->last_frame)&INT32_MAX;
    if(s->frame_valid && (delta==0 || delta>(uint32_t)INT32_MAX/2))return;
    s->frame_valid=true; s->last_frame=seq; s->last_result_ms=now;
    s->result=(vision_result_t){.valid=(mode==s->desired && mode!=ACTIVITY_OFF && !s->cloud_requested),
        .mode=mode,.epoch=epoch,.frame=seq,.count=values[0],.left=values[1],
        .center=values[2],.right=values[3],.received_ms=now};
    snprintf(s->result.state,sizeof(s->result.state),"%s",f[4]);
}
static int hex_digit(unsigned char c) {
    if(c>='0' && c<='9')return c-'0';
    if(c>='A' && c<='F')return c-'A'+10;
    if(c>='a' && c<='f')return c-'a'+10;
    return -1;
}
static void line(vision_session_t *s,uint64_t now) {
    size_t n=s->used;
    if(n && s->line[n-1]=='\r')--n;
    if(n<6 || s->line[n-5]!='*') { ++s->protocol_errors; return; }
    uint16_t received=0;
    for(size_t i=n-4;i<n;i++) {
        int digit=hex_digit((unsigned char)s->line[i]);
        if(digit<0) { ++s->protocol_errors; return; }
        received=(uint16_t)((received<<4)|digit);
    }
    if(received!=vision_crc16((uint8_t *)s->line,n-5)) { ++s->protocol_errors; return; }
    s->line[n-5]=0; handle(s,s->line,now);
}
void vision_session_feed(vision_session_t *s,const uint8_t *data,size_t n,uint64_t now) {
    if(!s->configured)return;
    if(s->byte_seen && now-s->last_byte_ms>500) { s->used=0; s->dropping=false; }
    if(n) { s->last_byte_ms=now; s->byte_seen=true; }
    while(n--) {
        uint8_t c=*data++;
        if(c=='\n') {
            if(!s->dropping && s->used)line(s,now);
            s->used=0; s->dropping=false;
        } else if(!s->dropping) {
            if(s->used>=sizeof(s->line)-1 || (c!='\r' && (c<32 || c>126))) {
                s->used=0; s->dropping=true; ++s->protocol_errors;
            } else s->line[s->used++]=(char)c;
        }
    }
}
void vision_session_tick(vision_session_t *s,uint64_t now,bool cloud_stopped) {
    if(s->cloud_requested && cloud_stopped) {
        s->cloud_requested=false;
        if(s->desired==ACTIVITY_CLOUD) {
            s->desired=ACTIVITY_OFF;
            snprintf(s->last_error,sizeof(s->last_error),"CLOUD_ENDED");
        }
    }
    if(s->configured && s->ack_valid && !s->pending && now-s->last_result_ms>1000)
        vision_session_fault(s,"RESULT_TIMEOUT");
    bool idle_confirmed=!s->configured || (s->synchronized && s->ack_valid && !s->pending && s->remote_mode==ACTIVITY_OFF);
    bool want_cloud=s->desired==ACTIVITY_CLOUD && idle_confirmed;
    if(!want_cloud && (s->cloud_requested || !cloud_stopped) && !s->cloud_stop_sent) {
        if(s->cloud && s->cloud(s->context,false))s->cloud_stop_sent=true;
        else snprintf(s->last_error,sizeof(s->last_error),"CLOUD_STOP_FAILED");
        s->cloud_requested=false;
    }
    if(cloud_stopped && !s->cloud_requested)s->cloud_stop_sent=false;
    if(!s->configured) {
        if(s->desired==ACTIVITY_LIGHT || s->desired==ACTIVITY_PERSON) {
            s->phase=VISION_UNCONFIGURED; s->result.valid=false; return;
        }
    } else {
        if(!s->synchronized) {
            s->phase=VISION_SYNCING;
            if(!s->sync_sent || now-s->sync_ms>=2000) {
                if(send_body(s,"SYNC")) { s->sync_ms=now; s->sync_sent=true; }
            }
            return;
        }
        activity_mode_t target=ACTIVITY_OFF;
        if(cloud_stopped && (s->desired==ACTIVITY_LIGHT || s->desired==ACTIVITY_PERSON))target=s->desired;
        if((s->pending && s->pending_mode!=target) || (!s->pending && (!s->ack_valid || s->remote_mode!=target))) {
            if(s->sequence==INT32_MAX) { vision_session_fault(s,"SEQ_WRAP"); return; }
            s->pending=true; s->pending_seq=++s->sequence; s->pending_mode=target;
            s->ack_valid=false; s->result.valid=false; s->attempts=0; transmit(s,now);
        } else if(s->pending && now-s->sent_ms>=20000) {
            if(s->attempts<3)transmit(s,now);
            else { vision_session_fault(s,"ACK_TIMEOUT"); return; }
        }
        if(s->pending) { s->phase=VISION_SWITCHING; return; }
    }
    if(s->desired==ACTIVITY_CLOUD) {
        if(!s->cloud_requested && cloud_stopped) {
            if(s->cloud && s->cloud(s->context,true)) {
                s->cloud_requested=true; s->cloud_stop_sent=false;
            } else { snprintf(s->last_error,sizeof(s->last_error),"CLOUD_START_FAILED"); }
        }
        s->phase=s->cloud_requested ? VISION_CLOUD_REQUESTED : VISION_WAIT_CLOUD_STOP;
    } else if(!cloud_stopped) {
        s->result.valid=false; s->phase=VISION_WAIT_CLOUD_STOP;
    } else s->phase=s->desired==ACTIVITY_OFF ? VISION_IDLE : VISION_ACTIVE;
}
