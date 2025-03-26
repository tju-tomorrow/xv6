#include "types.h"
#include "user.h"
#include "date.h"

int
main(int argc, char *argv[])
{
  struct rtcdate r;

  if (date(&r)) {
    printf(2, "date failed\n");
    exit();
  }

  // 打印当前UTC时间，格式：YYYY-MM-DD HH:MM:SS
  printf(1, "%d-%02d-%02d %02d:%02d:%02d\n", 
         r.year, r.month, r.day,
         r.hour, r.minute, r.second);

  exit();
}
