/*
 * 定位邮件正文构造：纯文本格式化，不执行定位查询、串口访问或邮件提交。
 */
#include "location_mail.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
/*
 * 构造 UTF-8 正文，所有长度上限均按字节计算，不按汉字数量计算。
 * @param out 输出缓冲区，必须与 base_body 分离；有效缓冲区在失败时置为空串。
 * @param capacity 输出容量（包含结尾 NUL），生成正文还受 768 字节上限限制。
 * @param base_body 基础说明，最长 400 字节。
 * @param location 本次查询结果；有效位置再次校验，无效位置写明状态并允许发信。
 * @param query_epoch 查询完成 UTC Unix 秒数；不表示定位解算时刻或位置年龄。
 * @return true 表示正文生成成功；false 表示参数、坐标或容量检查失败。
 * 不转换地图坐标系，不查缓存位置，也不把无效定位替换为旧坐标。
 */
bool location_mail_body(char *out, size_t capacity, const char *base_body,
                        const location_result_t *location, int64_t query_epoch)
{
    if (!out || !capacity) return false;
    out[0]=0;
    if (!base_body || strlen(base_body)>400 || !location || query_epoch<=0 ||
        (location->source!=LOCATION_LBS && location->source!=LOCATION_GNSS) ||
        location->status<LOCATION_VALID || location->status>LOCATION_IO_ERROR) return false;
    bool valid=location->status==LOCATION_VALID;
    if (valid && (!isfinite(location->longitude) || !isfinite(location->latitude) ||
        fabs(location->longitude)>180 || fabs(location->latitude)>90 ||
        (location->longitude==0 && location->latitude==0))) return false;
    char detail[200];
    if (valid) {
        snprintf(detail,sizeof(detail),"WGS84 经度: %.7f，纬度: %.7f\r\n%s\r\n",
            location->longitude,location->latitude,
            location->source==LOCATION_LBS ? "基站估算位置，误差可能较大。" : "GNSS报告位置，未验证精度及位置年龄。");
    } else {
        snprintf(detail,sizeof(detail),"无有效位置；状态: %s。未使用旧坐标。\r\n",
            location_status_name(location->status));
    }
    int n=snprintf(out,capacity,"%s\r\n位置来源: %s\r\n%s查询完成Unix时间(UTC): %" PRId64
        "\r\n查询时间不代表定位数据产生时间。\r\n",base_body,
        location->source==LOCATION_LBS ? "LBS" : "GNSS",detail,query_epoch);
    if (n<0 || (size_t)n>=capacity || n>768) {out[0]=0;return false;}
    return true;
}
