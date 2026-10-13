/*
 * 纯 C 网络时间解析：校验完整帧和公历日期，按 DTU 时区偏移换算 UTC。
 */
#include "dtu_time.h"
#include <string.h>
#include <stdlib.h>
/*
 * 公历闰年规则：四年一闰，整百年除外，四百年仍为闰年。
 */
static bool leap(unsigned y) {return y%4==0 && (y%100!=0 || y%400==0);}
/*
 * frame 必须是完整 CRLF 应答，包含 config/nettime/ok 和七个日期时间字段。
 * offset_minutes 描述模块报告时区；UTC = 模块日期秒数 - 偏移分钟 × 60。
 * 检查 2025—2100 年、闰年、时分秒和星期范围；失败时清零可写输出。
 */
bool dtu_time_parse(const char *frame, int offset_minutes, int64_t *utc_epoch)
{
    if (!utc_epoch) return false;
    *utc_epoch=0;
    if (!frame || offset_minutes < -720 || offset_minutes>840) return false;
    size_t n=strlen(frame);
    if (n<2 || n>=128 || strcmp(frame+n-2,"\r\n")) return false;
    char b[128];memcpy(b,frame,n-2);b[n-2]=0;
    char *f[10]={b};unsigned count=1;
    for(char *p=b;*p;p++) if (*p==',') {if(count==10)return false;*p=0;f[count++]=p+1;}
    if(count!=10 || strcmp(f[0],"config") || strcmp(f[1],"nettime") || strcmp(f[2],"ok"))return false;
    unsigned v[7];
    for(unsigned i=0;i<7;i++) {
        if(!f[i+3][0] || strlen(f[i+3])>4)return false;
        for(char *p=f[i+3];*p;p++)if(*p<'0' || *p>'9')return false;
        v[i]=(unsigned)strtoul(f[i+3],NULL,10);
    }
    unsigned y=v[0],m=v[1],day=v[2];
    static const unsigned md[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(y<2025 || y>2100 || m<1 || m>12 || day<1 || day>md[m-1]+(m==2 && leap(y)) ||
       v[3]>23 || v[4]>59 || v[5]>59 || v[6]<1 || v[6]>7)return false;
    int64_t days=0;
    for(unsigned year=1970;year<y;year++)days+=365+leap(year);
    for(unsigned month=1;month<m;month++)days+=md[month-1]+(month==2 && leap(y));
    days+=day-1;
    *utc_epoch=days*86400+(int64_t)v[3]*3600+v[4]*60+v[5]-(int64_t)offset_minutes*60;
    return true;
}
