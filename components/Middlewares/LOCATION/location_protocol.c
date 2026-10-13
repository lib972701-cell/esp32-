/*
 * 纯 C 定位应答解析：检查来源、字段、有效标志、方向和坐标边界。
 */
#include "location_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/*
 * 接受带可选正负号的小数文本；拒绝指数、NaN、无穷和多余字符。
 */
static bool decimal(const char *s, double *value)
{
    if (!s || !*s || strlen(s)>24) return false;
    const char *p=s;
    if (*p=='-' || *p=='+') p++;
    unsigned digits=0, dots=0;
    for (;*p;p++) {
        if (*p>='0' && *p<='9') digits++;
        else if (*p=='.') dots++;
        else return false;
    }
    if (!digits || dots>1) return false;
    char *end;
    *value=strtod(s,&end);
    return !*end && isfinite(*value);
}

/*
 * 解析完整 CRLF 帧，先初始化失败结果，再按 LBS/GNSS 字段格式校验。
 * GNSS 西经、南纬转为负数；双零为当前模块的无位置标记。
 * 只有 LOCATION_VALID 才发布坐标；接口不验证实际精度或定位数据年龄。
 */
location_status_t location_parse(const char *frame, location_source_t source, location_result_t *out)
{
    if (!out) return LOCATION_BAD_REPLY;
    *out=(location_result_t){.source=source,.status=LOCATION_BAD_REPLY};
    if (!frame || (source!=LOCATION_LBS && source!=LOCATION_GNSS)) return out->status;
    size_t n=strlen(frame);
    if (n<2 || n>=384 || strcmp(frame+n-2,"\r\n")) return out->status;
    char copy[384];memcpy(copy,frame,n-2);copy[n-2]=0;
    char *fields[16]={copy};size_t count=1;
    for (char *p=copy;*p;p++) {
        if (*p=='\r' || *p=='\n') return out->status;
        if (*p==',') {
            if (count==16) return out->status;
            *p=0;fields[count++]=p+1;
        }
    }
    if (count<3 || strcmp(fields[0],"config") ||
        strcmp(fields[1],source==LOCATION_LBS ? "lbsloc" : "gps")) return out->status;
    if (!strcmp(fields[2],"error")) {
        if (count!=4 || strlen(fields[3])!=1 || fields[3][0]<'1' || fields[3][0]>'3') return out->status;
        out->error_code=fields[3][0]-'0';
        return out->status=LOCATION_DEVICE_ERROR;
    }
    if (strcmp(fields[2],"ok")) return out->status;
    double lon,lat;
    if (source==LOCATION_LBS) {
        if (count!=5 || !decimal(fields[3],&lon) || !decimal(fields[4],&lat)) return out->status;
    } else {
        if (count!=14 || (strcmp(fields[3],"0") && strcmp(fields[3],"1"))) return out->status;
        if (!strcmp(fields[3],"0")) return out->status=LOCATION_NO_FIX;
        if ((strcmp(fields[4],"E") && strcmp(fields[4],"W")) ||
            (strcmp(fields[6],"N") && strcmp(fields[6],"S")) || !fields[13][0] ||
            !decimal(fields[5],&lon) || !decimal(fields[7],&lat) || lon<0 || lat<0) return out->status;
        if (!strcmp(fields[4],"W")) lon=-lon;
        if (!strcmp(fields[6],"S")) lat=-lat;
    }
    if (fabs(lon)>180 || fabs(lat)>90) return out->status;
    /* Both zeros are the actual DTU's unavailable-location sentinel. */
    if (lon==0 && lat==0) return out->status=LOCATION_NO_FIX;
    out->longitude=lon;out->latitude=lat;
    return out->status=LOCATION_VALID;
}
/*
 * 将状态枚举映射为稳定日志文本；越界值统一返回 BAD_REPLY。
 */
const char *location_status_name(location_status_t status)
{
    static const char *names[]={"VALID","NO_FIX","BAD_REPLY","DEVICE_ERROR","TIMEOUT","IO_ERROR"};
    return status>=LOCATION_VALID && status<=LOCATION_IO_ERROR ? names[status] : "BAD_REPLY";
}
