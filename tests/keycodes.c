#include <linux/input-event-codes.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
  // Physical QWERTY positions on standard evdev keyboard (XKB + 8)
  assert(KEY_C + 8 == 54);
  assert(KEY_V + 8 == 55);
  assert(KEY_T + 8 == 28);
  assert(KEY_W + 8 == 25);
  assert(KEY_1 + 8 == 10);
  assert(KEY_9 + 8 == 18);
  puts("Physical keycode mapping OK");
  return 0;
}
