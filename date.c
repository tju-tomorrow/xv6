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

  // 以易读的格式打印时间
  printf(1, "UTC time: %d-", r.year);
  printf(1, "%d-", r.month);
  printf(1, "%d ", r.day);
  printf(1, "%d:", r.hour);
  printf(1, "%d:", r.minute);
  printf(1, "%d\n", r.second);

  exit();
}
