/**
 * Smoke test for the C wrapper (rigel_c.h), compiled as plain C to prove
 * it's actually callable from a C translation unit, not just C++ with an
 * extern "C" label that nothing ever exercises.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "rigel_c.h"

static int g_fail = 0;

static void check(int cond, const char* label) {
  if (cond) {
    printf("PASS: %s\n", label);
  } else {
    printf("FAIL: %s\n", label);
    g_fail++;
  }
}

int main(void) {
  const char* dir = "/tmp/rigel_test_c";
  mkdir(dir, 0755);

  check(rigel_c_write_meta(dir, "capi", 8, 4, 1000),
        "rigel_c_write_meta succeeds");

  RigelHandle* h = rigel_c_create();
  check(rigel_c_init_from_meta(h, dir), "rigel_c_init_from_meta succeeds");
  check(rigel_c_index_offset(h) == 1000, "rigel_c_index_offset reads back index_offset");
  check(strcmp(rigel_c_key(h), "capi") == 0, "rigel_c_key reads back key");

  unsigned char wbuf[8], rbuf[8];
  memset(wbuf, 'C', 8);
  check(rigel_c_write(h, 1000, wbuf, 8) == 8, "rigel_c_write succeeds");
  check(rigel_c_read(h, 1000, rbuf, 8) == 8 && memcmp(wbuf, rbuf, 8) == 0,
        "rigel_c_read matches what was written");

  check(rigel_c_write(h, 999, wbuf, 8) == -1, "rigel_c_write below index_offset fails");

  check(rigel_c_delete(h, 1000), "rigel_c_delete succeeds");
  check(rigel_c_read(h, 1000, rbuf, 8) == -1, "rigel_c_read fails after delete");

  rigel_c_write(h, 1001, wbuf, 8);
  check(rigel_c_scan_init(h, 0), "rigel_c_scan_init succeeds");
  check(rigel_c_scan_next(h) == 1001, "rigel_c_scan_next returns the offset-shifted index");
  check(rigel_c_scan_next(h) == -1, "rigel_c_scan_next exhausts after one record");

  rigel_c_destroy(h);

  if (g_fail == 0) {
    printf("All tests passed\n");
  } else {
    printf("%d test(s) failed\n", g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
