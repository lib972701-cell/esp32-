#include <stdio.h>
#include <string.h>
#include "fall_mail.h"
#define assert(condition) do { if (!(condition)) { printf("FAIL line %d\n", __LINE__); return 1; } } while (0)
int main(void) {
 char body[401];
 assert(fall_mail_body(body,sizeof(body),7,123456));
 assert(strstr(body,"疑似摔倒") && strstr(body,"7"));
 assert(!fall_mail_body(body,10,7,123456));
 assert(fall_mail_retry(MAIL_FAILED,1));
 assert(!fall_mail_retry(MAIL_FAILED,3));
 assert(!fall_mail_retry(MAIL_UNKNOWN,1));
 assert(!fall_mail_retry(MAIL_ACCEPTED,1));
 assert(!fall_mail_retry(MAIL_REJECTED,1));
 return 0;
}
