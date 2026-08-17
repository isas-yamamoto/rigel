/**
 * Simple functional test for the Rigel class.
 *
 * Creates real data/index files and checks Write/Read consistency,
 * splitting across multiple files (rollover), Scan enumeration, and
 * safe failure on invalid indices.
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <thread>
#include <vector>
#include "rigel.h"

namespace {

// Best-effort count of this process's open file descriptors, via
// /proc/self/fd (Linux-specific). Returns -1 if unavailable (e.g. on a
// non-Linux platform), in which case the caller should skip the check
// rather than treat it as a failure.
int CountOpenFds() {
  DIR* d = ::opendir("/proc/self/fd");
  if (d == NULL) {
    return -1;
  }
  int count = 0;
  struct dirent* ent;
  while ((ent = ::readdir(d)) != NULL) {
    if (ent->d_name[0] != '.') {
      count++;
    }
  }
  ::closedir(d);
  return count;
}

} // namespace

static int g_fail = 0;

static void check(bool cond, const char* label) {
  if (cond) {
    std::printf("PASS: %s\n", label);
  } else {
    std::printf("FAIL: %s\n", label);
    g_fail++;
  }
}

int main() {
  const char* dir = "/tmp/rigel_test";
  ::mkdir(dir, 0755);

  // block_size=64, max_file_count=2 -> max_file_size=128 -> 2 blocks per file
  const int block_size = 64;
  const int max_file_count = 2;

  rigel::Rigel rigel;
  rigel.Init(dir, "t", block_size, max_file_count);

  unsigned char wbuf[4][block_size];
  for (int i = 0; i < 4; ++i) {
    std::memset(wbuf[i], 'A' + i, block_size);
  }

  // indices 0,1 land in the first file, 2,3 in the second
  for (int i = 0; i < 4; ++i) {
    ssize_t r = rigel.Write(i, wbuf[i], block_size);
    check(r == (ssize_t)block_size, "Write returns full block size");
  }

  for (int i = 0; i < 4; ++i) {
    unsigned char rbuf[block_size];
    ssize_t r = rigel.Read(i, rbuf, block_size);
    check(r == (ssize_t)block_size, "Read returns full block size");
    check(std::memcmp(rbuf, wbuf[i], block_size) == 0, "Read data matches Write data");
  }

  // An index that was never written has no flag set in the index file
  {
    unsigned char rbuf[block_size];
    ssize_t r = rigel.Read(100, rbuf, block_size);
    check(r == -1, "Read of unwritten index fails");
  }

  // A clearly out-of-range index pushes file_index past MAX_FILE_INDEX and
  // fails safely
  {
    unsigned char rbuf[block_size];
    long long huge_index = (long long)rigel::MAX_FILE_INDEX * max_file_count + 1000;
    ssize_t r = rigel.Read((int)huge_index, rbuf, block_size);
    check(r == -1, "Read of out-of-range index fails safely");
  }

  // Scan enumerates the written indices 0..3
  {
    bool seen[4] = {false, false, false, false};
    int count = 0;
    check(rigel.ScanInit(), "ScanInit succeeds");
    int idx;
    while ((idx = rigel.ScanNext()) >= 0) {
      if (idx >= 0 && idx < 4) {
        seen[idx] = true;
      }
      count++;
      if (count > 1000) {
        break; // guard against an infinite loop
      }
    }
    check(seen[0] && seen[1] && seen[2] && seen[3], "Scan enumerates all written indices");
  }

  // ScanInit(start) skips indices before start
  {
    check(rigel.ScanInit(2), "ScanInit(start=2) succeeds");
    int idx = rigel.ScanNext();
    check(idx == 2, "ScanInit(start) skips indices before start");
  }

  // Init(dirname) via metadata
  {
    const char* meta_dir = "/tmp/rigel_test_meta";
    ::mkdir(meta_dir, 0755);
    check(rigel::Rigel::WriteMeta(meta_dir, "metatest", 64, 2), "WriteMeta succeeds");

    rigel::Rigel r2;
    check(r2.Init(meta_dir), "Init(dirname) reads metadata successfully");

    unsigned char wbuf2[64], rbuf2[64];
    std::memset(wbuf2, 'Z', 64);
    check(r2.Write(0, wbuf2, 64) == 64, "Write via metadata-initialized Rigel succeeds");
    check(r2.Read(0, rbuf2, 64) == 64 && std::memcmp(wbuf2, rbuf2, 64) == 0,
          "Read via metadata-initialized Rigel matches");

    rigel::Rigel r3;
    check(!r3.Init("/tmp/rigel_test_meta_nonexistent"),
          "Init(dirname) fails when metadata is missing");
  }

  // A key containing a path separator (e.g. from a hand-planted or
  // adversarial rigel.meta) must be rejected, not used to build paths -
  // otherwise "key=../../etc/passwd" could write/read outside dirname.
  {
    check(!rigel::Rigel::WriteMeta("/tmp/rigel_test_traversal", "../evil", 64, 2),
          "WriteMeta rejects a key containing '/'");
    check(!rigel::Rigel::WriteMeta("/tmp/rigel_test_traversal", "", 64, 2),
          "WriteMeta rejects an empty key");

    const char* traversal_dir = "/tmp/rigel_test_traversal";
    ::mkdir(traversal_dir, 0755);
    FILE* f = std::fopen("/tmp/rigel_test_traversal/rigel.meta", "w");
    if (f != NULL) {
      std::fprintf(f, "key=../../../../tmp/rigel_test_traversal_escaped\n");
      std::fprintf(f, "block_size=64\n");
      std::fprintf(f, "max_file_count=2\n");
      std::fclose(f);
    }
    rigel::Rigel r5;
    check(!r5.Init(traversal_dir),
          "Init(dirname) rejects a hand-planted rigel.meta with a traversal key");
  }

  // LastError(): not set for a normal failure (reading an unwritten index),
  // but populated with a specific reason after misuse (out-of-range index).
  {
    const char* err_dir = "/tmp/rigel_test_lasterror";
    ::mkdir(err_dir, 0755);

    rigel::Rigel r4;
    r4.Init(err_dir, "err", 64, 2);

    unsigned char rbuf[64];
    check(r4.Read(0, rbuf, 64) == -1, "Read of unwritten index still fails");
    check(std::strlen(r4.LastError()) == 0,
          "LastError is empty after a normal not-found Read (not a real error)");

    long long huge_index = (long long)rigel::MAX_FILE_INDEX * 2 + 1000;
    unsigned char wbuf3[64];
    std::memset(wbuf3, 0, 64);
    ssize_t r = r4.Write((int)huge_index, wbuf3, 64);
    check(r == -1, "Write of out-of-range index fails");
    check(std::strstr(r4.LastError(), "out of range") != NULL,
          "LastError reports the out-of-range reason after misuse");
  }

  // Shard eviction: touching far more shards than the internal cache holds
  // must not accumulate one open fd per shard forever, and every record
  // must still read back correctly afterwards (an evicted shard has to be
  // transparently reopened).
  {
    const char* evict_dir = "/tmp/rigel_test_evict";
    ::mkdir(evict_dir, 0755);

    // block_size=8, max_file_count=1 -> one shard per index, so this
    // touches this many distinct shards. Comfortably larger than the
    // internal shard cache (a few thousand vs. its low-thousands cap) so
    // the bound is obvious rather than close to the cap itself.
    const int evict_block_size = 8;
    const int num_shards = 5000;

    rigel::Rigel evict_rigel;
    evict_rigel.Init(evict_dir, "evict", evict_block_size, 1);

    unsigned char buf[evict_block_size];
    int fds_before = CountOpenFds();
    for (int i = 0; i < num_shards; ++i) {
      std::memset(buf, (unsigned char)(i & 0xff), evict_block_size);
      evict_rigel.Write(i, buf, evict_block_size);
    }
    int fds_after = CountOpenFds();

    if (fds_before < 0 || fds_after < 0) {
      std::printf("SKIP: shard eviction fd-count check (/proc/self/fd unavailable)\n");
    } else {
      int fds_used = fds_after - fds_before;
      check(fds_used < num_shards / 2,
            "touching many shards does not accumulate one fd per shard");
    }

    bool all_match = true;
    for (int i = 0; i < num_shards; ++i) {
      unsigned char rbuf[evict_block_size];
      ssize_t r = evict_rigel.Read(i, rbuf, evict_block_size);
      unsigned char expected = (unsigned char)(i & 0xff);
      if (r != (ssize_t)evict_block_size) {
        all_match = false;
        break;
      }
      for (int b = 0; b < evict_block_size; ++b) {
        if (rbuf[b] != expected) {
          all_match = false;
          break;
        }
      }
    }
    check(all_match, "all records read back correctly after evicted shards are reopened");
  }

  // Thread safety: concurrent Write/Read from multiple threads on one
  // shared Rigel instance must not corrupt anything (writes span multiple
  // shards, contending on data_maps_ insertion too).
  {
    const char* mt_dir = "/tmp/rigel_test_mt";
    ::mkdir(mt_dir, 0755);

    const int mt_block_size = 64;
    const int mt_max_file_count = 2; // shard changes every 2 indices
    const int num_threads = 8;
    const int per_thread = 50;

    rigel::Rigel mt_rigel;
    mt_rigel.Init(mt_dir, "mt", mt_block_size, mt_max_file_count);

    // vector<bool> is bit-packed, so concurrent writes to different indices
    // from different threads would touch the same backing word and race
    // (confirmed with TSan). Use char instead.
    std::vector<char> write_ok(num_threads * per_thread, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
      threads.push_back(std::thread([&, t]() {
        unsigned char buf[mt_block_size];
        for (int i = 0; i < per_thread; ++i) {
          int idx = t * per_thread + i;
          std::memset(buf, (unsigned char)(t * 16 + i % 16), mt_block_size);
          ssize_t r = mt_rigel.Write(idx, buf, mt_block_size);
          write_ok[idx] = (r == (ssize_t)mt_block_size);
        }
      }));
    }
    for (size_t i = 0; i < threads.size(); ++i) {
      threads[i].join();
    }

    bool all_write_ok = true;
    for (size_t i = 0; i < write_ok.size(); ++i) {
      if (!write_ok[i]) {
        all_write_ok = false;
      }
    }
    check(all_write_ok, "Concurrent Write from multiple threads all succeed");

    bool all_read_ok = true;
    for (int t = 0; t < num_threads; ++t) {
      for (int i = 0; i < per_thread; ++i) {
        int idx = t * per_thread + i;
        unsigned char rbuf[mt_block_size];
        ssize_t r = mt_rigel.Read(idx, rbuf, mt_block_size);
        unsigned char expected = (unsigned char)(t * 16 + i % 16);
        if (r != (ssize_t)mt_block_size) {
          all_read_ok = false;
          continue;
        }
        for (int b = 0; b < mt_block_size; ++b) {
          if (rbuf[b] != expected) {
            all_read_ok = false;
            break;
          }
        }
      }
    }
    check(all_read_ok, "Data written concurrently reads back correctly (no cross-thread corruption)");
  }

  if (g_fail == 0) {
    std::printf("All tests passed\n");
  } else {
    std::printf("%d test(s) failed\n", g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
