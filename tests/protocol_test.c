#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "mail_protocol.h"
typedef struct { const char *rx; size_t pos; char tx[4096]; size_t used; } fake_t;
static int rd(void *ctx, unsigned char *p, size_t n) {
    fake_t *f=ctx; (void)n;
    if (!f->rx[f->pos]) return -1;
    *p=(unsigned char)f->rx[f->pos++]; return 1;
}
static int wr(void *ctx, const unsigned char *p, size_t n) {
    fake_t *f=ctx;
    if (n>sizeof(f->tx)-1-f->used) return -1;
    memcpy(f->tx+f->used,p,n); f->used+=n; f->tx[f->used]=0; return (int)n;
}
int main(void) {
    assert(mail_address_valid("demo@qq.com"));
    assert(!mail_address_valid("a@qq.com\r\nRCPT TO:<evil@qq.com>"));
    assert(!mail_address_valid("a@qq.com,b@qq.com"));
    assert(!mail_address_valid("@qq.com"));
    assert(!mail_address_valid("a@@qq.com"));
    fake_t f={.rx="250-test\r\n250-AUTH LOGIN\r\n250 OK\r\n221 bye\r\n"};
    mail_io_t io={.ctx=&f,.read=rd,.write=wr};
    assert(mail_reply(&io)==250); assert(mail_reply(&io)==221);
    f=(fake_t){.rx="250-first\r\n550 bad\r\n"};
    assert(mail_reply(&io)<0);
    f=(fake_t){.rx="25x bad\r\n"}; assert(mail_reply(&io)<0);
    f=(fake_t){.rx="250 incomplete"}; assert(mail_reply(&io)<0);
    assert(mail_dtu_channel_valid("config,netchaninfo,ok,tcp,uart,0,0,00,60,smtp.qq.com,465,0,,0,,0,,0,0"));
    assert(!mail_dtu_channel_valid("config,netchaninfo,ok,tcp,uart,0,0,00,60,smtp.qq.com,465,0,,0,,0,,0,1"));
    assert(!mail_dtu_channel_valid("config,netchaninfo,ok,tcp,uart,0,0,00,60,wrong.com,465,0,,0,,0,,0,0"));
    f=(fake_t){.rx="220 hi\r\n"};
    assert(mail_submit(&io,"a@qq.com","b@qq.com","YQ==","Yg==\r\nMAIL FROM:<evil@qq.com>","body\r\n")==MAIL_FAILED);
    assert(f.used==0);
    f=(fake_t){.rx="220 hi\r\n250-hello\r\n250 AUTH LOGIN\r\n334 name\r\n334 password\r\n235 ok\r\n250 sender\r\n250 recipient\r\n354 data\r\n250 queued\r\n"};
    assert(mail_submit(&io,"a@qq.com","b@qq.com","YUBxcS5jb20=","c2VjcmV0","From: a@qq.com\r\n\r\nYm9keQ==\r\n")==MAIL_ACCEPTED);
    assert(strstr(f.tx,"\r\n.\r\n"));
    assert(!strstr(f.tx,"QUIT"));
    f=(fake_t){.rx="220 hi\r\n250 hi\r\n334 name\r\n334 password\r\n535 bad\r\n"};
    assert(mail_submit(&io,"a@qq.com","b@qq.com","YQ==","Yg==","body\r\n")==MAIL_REJECTED);
    assert(!strstr(f.tx,"MAIL FROM"));
    f=(fake_t){.rx="220 hi\r\n250 hi\r\n334 name\r\n334 password\r\n235 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n"};
    assert(mail_submit(&io,"a@qq.com","b@qq.com","YQ==","Yg==","body\r\n")==MAIL_UNKNOWN);
    f=(fake_t){.rx="220 hi\r\n250 hi\r\n334 name\r\n334 password\r\n235 ok\r\n250 ok\r\n250 ok\r\n354 go\r\n554 rejected\r\n"};
    assert(mail_submit(&io,"a@qq.com","b@qq.com","YQ==","Yg==","body\r\n")==MAIL_REJECTED);
    puts("PASS: address injection, fragmented replies, malformed/multiline replies, DTU config, SMTP accepted/auth rejection/DATA unknown/DATA rejection");
    return 0;
}
