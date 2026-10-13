/*
 * 纯 C SMTP 协议层：经抽象 I/O 处理应答与提交，便于在宿主环境回归测试。
 */
#include "mail_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*
 * 检查工程支持的有限邮箱格式与长度；不等价于完整 RFC 地址解析器。
 */
bool mail_address_valid(const char *s)
{
    if (!s || strlen(s)>120) return false;
    const char *at=strchr(s,'@');
    if (!at || at==s || strchr(at+1,'@') || !at[1]) return false;
    if (s[0]=='.' || at[-1]=='.' || strstr(s,"..")) return false;
    for (const char *p=s;p<at;p++)
        if (!isalnum((unsigned char)*p) && !strchr("._+-",*p)) return false;
    const char *dot=strrchr(at+1,'.');
    if (!dot || dot==at+1 || !dot[1] || at[1]=='-' || dot[-1]=='-') return false;
    for (const char *p=at+1;*p;p++)
        if (!isalnum((unsigned char)*p) && *p!='.' && *p!='-') return false;
    return true;
}

/*
 * 核验 DTU CSV 通道：TCP/UART 透传、smtp.qq.com:465 及关键关闭项。
 * 保留空字段，避免 CSV 索引偏移导致误判配置。
 */
bool mail_dtu_channel_valid(const char *line)
{
    const char *prefix="config,netchaninfo,ok,";
    if (strncmp(line,prefix,strlen(prefix))) return false;
    // Preserve empty CSV fields; strtok would silently shift option indexes.
    char fields[20][128]={{0}};
    unsigned f=0,n=0;
    for (const char *p=line+strlen(prefix);;p++) {
        if (*p==',' || !*p) {
            if (!*p) break;
            if (++f>=20) return false;
            n=0;
        } else {
            if (n>=127) return false;
            fields[f][n++]=*p;
        }
    }
    if (f<15 || strcmp(fields[0],"tcp") || strcmp(fields[1],"uart") ||
        strcmp(fields[2],"0") || strcmp(fields[6],"smtp.qq.com") || strcmp(fields[7],"465")) return false;
    const unsigned zero[]={8,10,12,14,15};
    for (unsigned i=0;i<sizeof(zero)/sizeof(zero[0]);i++)
        if (strcmp(fields[zero[i]],"0")) return false;
    return true;
}

/*
 * 循环处理短写，直至整个 NUL 结尾文本发送完毕；返回 false 表示失败。
 */
bool mail_write_all(mail_io_t *io, const char *text)
{
    size_t n=strlen(text),off=0;
    while (off<n) {
        int r=io->write(io->ctx,(const unsigned char *)text+off,n-off);
        if (r<=0 || (size_t)r>n-off) return false;
        off+=(size_t)r;
    }
    return true;
}

/*
 * 解析最多 64 行 SMTP 应答，要求 CRLF、三位状态码及一致的多行应答码。
 * 返回最终状态码，格式错误、超长或读取失败返回 -1。
 */
int mail_reply(mail_io_t *io)
{
    int expected=-1;
    for (unsigned lines=0;lines<64;lines++) {
        char line[513]; size_t n=0;
        for (;;) {
            unsigned char ch;
            if (n>=sizeof(line)-1 || io->read(io->ctx,&ch,1)!=1) return -1;
            line[n++]=(char)ch;
            if (ch=='\n') break;
        }
        line[n]=0;
        if (n<6 || line[n-2]!='\r' || !isdigit((unsigned char)line[0]) ||
            !isdigit((unsigned char)line[1]) || !isdigit((unsigned char)line[2]) ||
            (line[3]!='-' && line[3]!=' ')) return -1;
        int code=(line[0]-'0')*100+(line[1]-'0')*10+line[2]-'0';
        if (code<200 || code>599 || (expected!=-1 && code!=expected)) return -1;
        expected=code;
        if (line[3]==' ') return code;
    }
    return -1;
}

/*
 * 发送一个完整 SMTP 命令并读取应答；命令须自行包含 CRLF。
 */
static int command(mail_io_t *io, const char *cmd)
{
    return mail_write_all(io,cmd) ? mail_reply(io) : -1;
}
/*
 * 明确 4xx/5xx 映射为拒绝；读写或其他非预期应答映射为失败。
 */
static mail_result_t failure(int code)
{
    return code>=400 ? MAIL_REJECTED : MAIL_FAILED;
}
/*
 * 可选阶段通知回调，用于更新传输截止时间和输出不含凭据的阶段日志。
 */
static void stage(mail_io_t *io, const char *name)
{
    if (io->stage) io->stage(io->ctx,name);
}
/*
 * 按 greeting/EHLO/AUTH/MAIL/RCPT/DATA 顺序提交一次邮件。
 * 用户名与授权码应已 Base64 编码，MIME 正文亦须预先编码。
 * 进入正文发送后，传输失败或最终应答丢失返回 MAIL_UNKNOWN，禁止盲目重发。
 * MAIL_ACCEPTED 只表示服务器最终 250 接受提交，不保证收件箱实际到达。
 */
mail_result_t mail_submit(mail_io_t *io, const char *sender, const char *recipient,
                          const char *user_b64, const char *pass_b64, const char *mime)
{
    if (!mail_address_valid(sender) || !mail_address_valid(recipient)) return MAIL_FAILED;
    // Authorization tokens are base64 only: never permit a second SMTP command.
    const char *tokens[]={user_b64,pass_b64};
    for (unsigned i=0;i<2;i++) {
        if (!tokens[i] || !tokens[i][0] || strlen(tokens[i])>256) return MAIL_FAILED;
        for (const char *p=tokens[i];*p;p++)
            if (!isalnum((unsigned char)*p) && !strchr("+/=",*p)) return MAIL_FAILED;
    }
    char cmd[300]; stage(io,"SMTP_GREETING"); int code=mail_reply(io);
    if (code!=220) return failure(code);
    stage(io,"SMTP_EHLO");
    code=command(io,"EHLO esp32-mail.local\r\n"); if (code!=250) return failure(code);
    stage(io,"AUTH_LOGIN");
    code=command(io,"AUTH LOGIN\r\n"); if (code!=334) return failure(code);
    stage(io,"AUTH_USER");
    snprintf(cmd,sizeof(cmd),"%s\r\n",user_b64);
    code=command(io,cmd); if (code!=334) return failure(code);
    stage(io,"AUTH_SECRET");
    snprintf(cmd,sizeof(cmd),"%s\r\n",pass_b64);
    code=command(io,cmd);
    // Clear the copied base64 secret even on authentication failure.
    volatile char *wipe=cmd; for (size_t i=0;i<sizeof(cmd);i++) wipe[i]=0;
    if (code!=235) return failure(code);
    stage(io,"MAIL_FROM");
    snprintf(cmd,sizeof(cmd),"MAIL FROM:<%s>\r\n",sender);
    code=command(io,cmd); if (code!=250) return failure(code);
    stage(io,"RCPT_TO");
    snprintf(cmd,sizeof(cmd),"RCPT TO:<%s>\r\n",recipient);
    code=command(io,cmd); if (code!=250 && code!=251) return failure(code);
    stage(io,"DATA_COMMAND");
    code=command(io,"DATA\r\n"); if (code!=354) return failure(code);
    stage(io,"DATA_BODY");
    // MIME body must already be base64 encoded; no dot-stuffing is necessary.
    if (!mail_write_all(io,mime) || !mail_write_all(io,".\r\n")) return MAIL_UNKNOWN;
    stage(io,"DATA_RESPONSE");
    code=mail_reply(io);
    if (code==250) return MAIL_ACCEPTED;
    return code>=400 ? MAIL_REJECTED : MAIL_UNKNOWN;
}
