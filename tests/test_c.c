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
  check(rigel_c_init_from_meta(h, dir, 0), "rigel_c_init_from_meta succeeds");
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

  RigelCStat st;
  check(rigel_c_stat(h, &st) && st.record_count == 1, "rigel_c_stat reports record_count");
  check(!st.frozen, "rigel_c_stat reports frozen=0 before freezing");

  check(!rigel_c_frozen(h), "rigel_c_frozen is 0 before freezing");
  check(rigel_c_set_frozen(dir, 1), "rigel_c_set_frozen(1) succeeds");
  rigel_c_destroy(h);

  h = rigel_c_create();
  check(rigel_c_init_from_meta(h, dir, 0), "rigel_c_init_from_meta re-reads metadata after freezing");
  check(rigel_c_frozen(h), "rigel_c_frozen is 1 after freezing");
  check(rigel_c_write(h, 1001, wbuf, 8) == -1, "rigel_c_write fails on a frozen directory");
  check(!rigel_c_delete(h, 1001), "rigel_c_delete fails on a frozen directory");
  check(rigel_c_read(h, 1001, rbuf, 8) == 8, "rigel_c_read still works on a frozen directory");

  check(rigel_c_set_frozen(dir, 0), "rigel_c_set_frozen(0) succeeds");
  rigel_c_destroy(h);

  h = rigel_c_create();
  check(rigel_c_init_from_meta(h, dir, 0), "rigel_c_init_from_meta re-reads metadata after unfreezing");
  check(!rigel_c_frozen(h), "rigel_c_frozen is 0 after unfreezing");
  check(rigel_c_delete(h, 1001), "rigel_c_delete succeeds again after unfreezing");
  check(rigel_c_write(h, 1001, wbuf, 8) == 8, "rigel_c_write re-adds index 1001 for the read_only checks below");

  rigel_c_destroy(h);

  h = rigel_c_create();
  check(rigel_c_init_from_meta(h, dir, 1), "rigel_c_init_from_meta(read_only=1) succeeds");
  check(rigel_c_read_only(h), "rigel_c_read_only is 1 for a read_only handle");
  check(rigel_c_read(h, 1001, rbuf, 8) == 8, "rigel_c_read works on a read_only handle");
  check(rigel_c_write(h, 1001, wbuf, 8) == -1, "rigel_c_write fails on a read_only handle");
  check(!rigel_c_delete(h, 1001), "rigel_c_delete fails on a read_only handle");
  rigel_c_destroy(h);

  const char* empty_dir = "/tmp/rigel_test_c_empty";
  mkdir(empty_dir, 0755);
  h = rigel_c_create();
  rigel_c_init(h, empty_dir, "capi", 8, 4, 0, 1);
  check(rigel_c_read_only(h), "rigel_c_init(read_only=1) sets read_only even against a fresh dir");
  check(rigel_c_read(h, 0, rbuf, 8) == -1,
        "rigel_c_read on a read_only handle misses cleanly when the index file doesn't exist yet");
  rigel_c_destroy(h);

  /* Every function taking a RigelHandle* must tolerate NULL - reported
     through its normal failure return, not a segfault from a caller bug
     (e.g. skipping a NULL check on rigel_c_create()'s result). */
  rigel_c_init(NULL, empty_dir, "capi", 8, 4, 0, 0); /* must not crash */
  check(!rigel_c_init_from_meta(NULL, empty_dir, 0), "rigel_c_init_from_meta(NULL, ...) fails cleanly");
  check(rigel_c_write(NULL, 0, wbuf, 8) == -1, "rigel_c_write(NULL, ...) fails cleanly");
  check(rigel_c_read(NULL, 0, rbuf, 8) == -1, "rigel_c_read(NULL, ...) fails cleanly");
  check(!rigel_c_delete(NULL, 0), "rigel_c_delete(NULL, ...) fails cleanly");
  check(!rigel_c_scan_init(NULL, 0), "rigel_c_scan_init(NULL, ...) fails cleanly");
  check(rigel_c_scan_next(NULL) == -1, "rigel_c_scan_next(NULL) fails cleanly");
  check(rigel_c_last_error(NULL) != NULL, "rigel_c_last_error(NULL) returns a non-NULL string");
  check(rigel_c_block_size(NULL) == 0, "rigel_c_block_size(NULL) returns 0");
  check(rigel_c_key(NULL) != NULL, "rigel_c_key(NULL) returns a non-NULL string");
  check(rigel_c_max_file_count(NULL) == 0, "rigel_c_max_file_count(NULL) returns 0");
  check(rigel_c_index_offset(NULL) == 0, "rigel_c_index_offset(NULL) returns 0");
  check(!rigel_c_frozen(NULL), "rigel_c_frozen(NULL) returns 0");
  check(!rigel_c_read_only(NULL), "rigel_c_read_only(NULL) returns 0");
  check(!rigel_c_stat(NULL, &st), "rigel_c_stat(NULL, ...) fails cleanly");
  rigel_c_destroy(NULL); /* must not crash */

  if (g_fail == 0) {
    printf("All tests passed\n");
  } else {
    printf("%d test(s) failed\n", g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
