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

    // Calling Write/Read/Delete after Init(dirname) failed (its bool
    // return ignored) must fail cleanly, not divide by the still-zero
    // max_file_size_ (that used to be a SIGFPE crash, not a -1/false).
    unsigned char wbuf3[16], rbuf3[16];
    std::memset(wbuf3, 'Q', 16);
    check(r3.Write(0, wbuf3, 16) == -1, "Write on a never-successfully-Init'd handle fails cleanly");
    check(std::strstr(r3.LastError(), "not initialized") != NULL,
          "LastError names 'not initialized' as the reason");
    check(r3.Read(0, rbuf3, 16) == -1, "Read on a never-successfully-Init'd handle fails cleanly");
    check(!r3.Delete(0), "Delete on a never-successfully-Init'd handle fails cleanly");
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

  // A line starting with '#' is a comment and must be ignored even if it
  // contains what looks like a field assignment (e.g. "# key=trap"), so
  // rigel.meta can be hand-annotated without risk of a comment shadowing
  // the real value.
  {
    const char* comment_dir = "/tmp/rigel_test_comment";
    ::mkdir(comment_dir, 0755);
    FILE* f = std::fopen("/tmp/rigel_test_comment/rigel.meta", "w");
    if (f != NULL) {
      std::fprintf(f, "# hand-annotated rigel.meta\n");
      std::fprintf(f, "# key=trap\n");
      std::fprintf(f, "key=commented\n");
      std::fprintf(f, "block_size=8\n");
      std::fprintf(f, "max_file_count=4\n");
      std::fprintf(f, "# index_offset=9999\n");
      std::fprintf(f, "index_offset=1000\n");
      std::fclose(f);
    }
    rigel::Rigel r9;
    check(r9.Init(comment_dir), "Init(dirname) reads a rigel.meta with comment lines");
    check(std::strcmp(r9.Key(), "commented") == 0,
          "A '#'-commented field assignment doesn't shadow the real one");
    check(r9.IndexOffset() == 1000,
          "A '#'-commented index_offset doesn't shadow the real one");
  }

  // Delete: clears an index back to never-written (Read fails, Scan skips
  // it), and is a harmless no-op on an index that was never written.
  {
    const char* del_dir = "/tmp/rigel_test_delete";
    ::mkdir(del_dir, 0755);

    rigel::Rigel r6;
    r6.Init(del_dir, "del", 8, 2);

    unsigned char wbuf[8], rbuf[8];
    std::memset(wbuf, 'X', 8);
    check(r6.Write(0, wbuf, 8) == 8, "Delete test: Write succeeds");
    check(r6.Delete(0), "Delete succeeds on a written index");
    check(r6.Read(0, rbuf, 8) == -1, "Read fails after Delete");
    check(r6.Delete(0), "Delete is a no-op on an already-deleted index");
    check(r6.Delete(999), "Delete is a no-op on a never-written index");

    check(r6.Write(1, wbuf, 8) == 8, "Delete test: second Write succeeds");
    check(r6.ScanInit(), "Delete test: ScanInit succeeds");
    bool saw_0 = false, saw_1 = false;
    int idx;
    while ((idx = r6.ScanNext()) >= 0) {
      if (idx == 0) saw_0 = true;
      if (idx == 1) saw_1 = true;
    }
    check(!saw_0 && saw_1, "Scan skips a deleted index but still sees others");
  }

  // index_offset: shifts the externally visible index space (e.g. so
  // multiple tools agree that "index 1000" means the first record, not
  // "index 0"). WriteMeta/Init(dirname) round-trip it, and every
  // Write/Read/Delete/Scan call operates in the offset index space.
  {
    const char* off_dir = "/tmp/rigel_test_offset";
    ::mkdir(off_dir, 0755);
    check(rigel::Rigel::WriteMeta(off_dir, "off", 8, 4, 1000),
          "WriteMeta succeeds with a non-zero index_offset");

    rigel::Rigel r7;
    check(r7.Init(off_dir), "Init(dirname) reads a metadata file with index_offset");
    check(r7.IndexOffset() == 1000, "IndexOffset() reports the value from metadata");

    unsigned char wbuf[8], rbuf[8];
    std::memset(wbuf, 'Y', 8);
    check(r7.Write(1000, wbuf, 8) == 8, "Write at the offset index (first record) succeeds");
    check(r7.Read(1000, rbuf, 8) == 8 && std::memcmp(wbuf, rbuf, 8) == 0,
          "Read at the offset index matches");

    check(r7.Write(999, wbuf, 8) == -1, "Write below index_offset fails");
    check(r7.Read(999, rbuf, 8) == -1, "Read below index_offset fails");

    check(r7.Write(1001, wbuf, 8) == 8, "Write test: second record succeeds");
    check(r7.ScanInit(), "Offset test: ScanInit succeeds");
    bool saw_1000 = false, saw_1001 = false;
    int idx;
    while ((idx = r7.ScanNext()) >= 0) {
      if (idx == 1000) saw_1000 = true;
      if (idx == 1001) saw_1001 = true;
    }
    check(saw_1000 && saw_1001, "Scan returns indices already shifted by index_offset");

    check(r7.Delete(1000), "Delete at an offset index succeeds");
    check(r7.Read(1000, rbuf, 8) == -1, "Read fails after deleting an offset index");

    rigel::Rigel r8;
    r8.Init(off_dir, "off2", 8, 4); // index_offset defaults to 0
    check(r8.IndexOffset() == 0, "Init() without index_offset defaults to 0");
  }

  // Frozen directories: SetFrozen() rewrites rigel.meta in place, blocking
  // Write/Delete on any handle that (re)reads it while still allowing
  // Read/Scan - a guard against writing into finished, archival data.
  {
    const char* frozen_dir = "/tmp/rigel_test_frozen";
    ::mkdir(frozen_dir, 0755);
    check(rigel::Rigel::WriteMeta(frozen_dir, "fz", 8, 4),
          "WriteMeta succeeds (frozen defaults to false)");

    rigel::Rigel r9;
    check(r9.Init(frozen_dir), "Init(dirname) reads freshly-written metadata");
    check(!r9.Frozen(), "Frozen() is false before freezing");

    unsigned char wbuf[8], rbuf[8];
    std::memset(wbuf, 'F', 8);
    check(r9.Write(0, wbuf, 8) == 8, "Write succeeds before freezing");

    check(rigel::Rigel::SetFrozen(frozen_dir, true), "SetFrozen(true) succeeds");

    rigel::Rigel r10;
    check(r10.Init(frozen_dir), "Init(dirname) re-reads metadata after freezing");
    check(r10.Frozen(), "Frozen() is true after freezing");
    check(r10.Write(1, wbuf, 8) == -1, "Write fails on a frozen directory");
    check(std::strstr(r10.LastError(), "frozen") != NULL,
          "LastError names 'frozen' as the reason a frozen Write failed");
    check(!r10.Delete(0), "Delete fails on a frozen directory");
    check(r10.Read(0, rbuf, 8) == 8 && std::memcmp(wbuf, rbuf, 8) == 0,
          "Read still works on a frozen directory");
    check(r10.ScanInit(), "ScanInit still works on a frozen directory");

    rigel::Rigel::Stat st;
    check(r10.GetStat(&st) && st.frozen, "GetStat() reports frozen");

    check(rigel::Rigel::SetFrozen(frozen_dir, false), "SetFrozen(false) succeeds");
    rigel::Rigel r11;
    check(r11.Init(frozen_dir), "Init(dirname) re-reads metadata after unfreezing");
    check(!r11.Frozen(), "Frozen() is false after unfreezing");
    check(r11.Write(1, wbuf, 8) == 8, "Write succeeds again after unfreezing");

    check(!rigel::Rigel::SetFrozen("/tmp/rigel_test_frozen_missing", true),
          "SetFrozen fails for a directory with no metadata");
  }

  // Read-only handles (Init's read_only param): opens data/index files
  // O_RDONLY/mmaps PROT_READ instead of O_RDWR|O_CREAT/PROT_READ|WRITE, so
  // Read/Scan work even with no write permission on the directory at all
  // (e.g. a read-only NFS export) - unlike Frozen(), which is a flag
  // persisted in rigel.meta, this is local to the handle that asked for it.
  {
    const char* ro_dir = "/tmp/rigel_test_readonly";
    ::mkdir(ro_dir, 0755);
    check(rigel::Rigel::WriteMeta(ro_dir, "ro", 8, 4), "WriteMeta succeeds");

    unsigned char wbuf[8], rbuf[8];
    std::memset(wbuf, 'R', 8);
    {
      rigel::Rigel writer;
      check(writer.Init(ro_dir), "Init(dirname) succeeds for the writer handle");
      check(writer.Write(0, wbuf, 8) == 8, "Write succeeds via the non-read-only handle");
    }

    rigel::Rigel reader;
    check(reader.Init(ro_dir, /*read_only=*/true), "Init(dirname, read_only=true) succeeds");
    check(reader.ReadOnly(), "ReadOnly() is true");
    check(reader.Read(0, rbuf, 8) == 8 && std::memcmp(wbuf, rbuf, 8) == 0,
          "Read on a read_only handle returns what a prior writer wrote");
    check(reader.Write(1, wbuf, 8) == -1, "Write fails on a read_only handle");
    check(std::strstr(reader.LastError(), "read-only") != NULL,
          "LastError names read-only as the reason a read_only Write failed");
    check(!reader.Delete(0), "Delete fails on a read_only handle");
    check(reader.ScanInit() && reader.ScanNext() == 0, "ScanInit/ScanNext work on a read_only handle");

    // chmod the directory itself read-only (no write bit) to prove this
    // isn't just "the fd happens to be opened O_RDONLY" but that the
    // implementation genuinely never needs write access to work.
    ::chmod(ro_dir, 0555);
    rigel::Rigel chmod_reader;
    bool chmod_ok = chmod_reader.Init(ro_dir, /*read_only=*/true) &&
                    chmod_reader.Read(0, rbuf, 8) == 8;
    ::chmod(ro_dir, 0755); // restore before any cleanup that might need to write here
    check(chmod_ok, "Read works against a directory with no write permission at all");

    // A fresh, never-written directory (no index file yet): a read_only
    // handle must not try to create one - Read degrades to a normal miss.
    const char* ro_empty_dir = "/tmp/rigel_test_readonly_empty";
    ::mkdir(ro_empty_dir, 0755);
    rigel::Rigel empty_reader;
    empty_reader.Init(ro_empty_dir, "roempty", 8, 4, 0, /*read_only=*/true);
    check(empty_reader.Read(0, rbuf, 8) == -1,
          "Read on a read_only handle misses cleanly when the index file doesn't exist yet");
  }

  // Auto-detected read-only: a handle opened *without* read_only=true still
  // falls back to a read-only open (and permanently flips ReadOnly() to
  // true) the moment it hits EACCES/EROFS on a Read/ScanInit-driven open -
  // so a caller doesn't have to know in advance that a directory turned
  // out to be write-restricted (e.g. remounted read-only under it).
  {
    const char* auto_dir = "/tmp/rigel_test_readonly_auto";
    ::mkdir(auto_dir, 0755);
    check(rigel::Rigel::WriteMeta(auto_dir, "auto", 8, 4), "WriteMeta succeeds");

    unsigned char wbuf[8], rbuf[8];
    std::memset(wbuf, 'A', 8);
    {
      rigel::Rigel writer;
      check(writer.Init(auto_dir), "Init(dirname) succeeds for the writer handle");
      check(writer.Write(0, wbuf, 8) == 8, "Write succeeds before chmod'ing the files read-only");
    }

    char shard_path[MAXPATHLEN + 32], index_path[MAXPATHLEN + 32];
    std::snprintf(shard_path, sizeof(shard_path), "%s/auto.0000", auto_dir);
    std::snprintf(index_path, sizeof(index_path), "%s/auto.index", auto_dir);
    ::chmod(shard_path, 0444);
    ::chmod(index_path, 0444);

    rigel::Rigel auto_reader;
    check(auto_reader.Init(auto_dir), "Init(dirname) succeeds (read_only left at its default false)");
    check(!auto_reader.ReadOnly(), "ReadOnly() is false before the first Read attempt");
    check(auto_reader.Read(0, rbuf, 8) == 8 && std::memcmp(wbuf, rbuf, 8) == 0,
          "Read succeeds anyway via the automatic EACCES fallback");
    check(auto_reader.ReadOnly(), "ReadOnly() is true after the fallback triggered");

    // Write must fail cleanly through the ordinary read_only_ guard at its
    // own entry (checked before this handle ever touches a shard/index
    // fd), not by trying to write through the PROT_READ mapping the
    // fallback above just created.
    check(auto_reader.Write(1, wbuf, 8) == -1, "Write fails after auto-detected read_only");
    check(std::strstr(auto_reader.LastError(), "read-only") != NULL,
          "LastError names read-only as the reason, from the auto-detected state");

    // Write() itself must never trigger the fallback: called first (no
    // prior Read), it should fail with a plain permission error and leave
    // ReadOnly() false, rather than silently switching modes and then
    // segfaulting trying to write into a PROT_READ mapping.
    rigel::Rigel writer_first;
    check(writer_first.Init(auto_dir), "Init(dirname) succeeds for a second, fresh handle");
    check(writer_first.Write(1, wbuf, 8) == -1,
          "Write called first (no prior Read) still fails, not falls back");
    check(!writer_first.ReadOnly(),
          "ReadOnly() stays false - Write never auto-detects, it just fails");

    ::chmod(shard_path, 0644);
    ::chmod(index_path, 0644);
  }

  // SetFrozen() must not clobber hand-added '#' comments in rigel.meta -
  // it used to go through WriteMeta(), which regenerates the whole file
  // and silently dropped them.
  {
    const char* commented_dir = "/tmp/rigel_test_frozen_comments";
    ::mkdir(commented_dir, 0755);
    char meta_path[MAXPATHLEN + 32];
    std::snprintf(meta_path, sizeof(meta_path), "%s/rigel.meta", commented_dir);

    FILE* f = std::fopen(meta_path, "w");
    std::fprintf(f, "# hand-written comment above key\n");
    std::fprintf(f, "key=commented\n");
    std::fprintf(f, "block_size=8\n");
    std::fprintf(f, "max_file_count=4\n");
    std::fprintf(f, "# hand-written comment at the end\n");
    std::fclose(f);

    check(rigel::Rigel::SetFrozen(commented_dir, true), "SetFrozen(true) succeeds");

    f = std::fopen(meta_path, "r");
    char contents[1024];
    size_t n = std::fread(contents, 1, sizeof(contents) - 1, f);
    contents[n] = '\0';
    std::fclose(f);

    check(std::strstr(contents, "# hand-written comment above key") != NULL,
          "SetFrozen preserves a comment line above the fields");
    check(std::strstr(contents, "# hand-written comment at the end") != NULL,
          "SetFrozen preserves a comment line after the fields");
    check(std::strstr(contents, "frozen=1") != NULL,
          "SetFrozen still adds the frozen=1 line");

    rigel::Rigel r12;
    check(r12.Init(commented_dir) && r12.Frozen(),
          "the directory reads back as frozen despite the comments");

    // Freezing again (already frozen -> frozen) must not duplicate the line.
    check(rigel::Rigel::SetFrozen(commented_dir, true), "SetFrozen(true) again succeeds");
    f = std::fopen(meta_path, "r");
    n = std::fread(contents, 1, sizeof(contents) - 1, f);
    contents[n] = '\0';
    std::fclose(f);
    int frozen_line_count = 0;
    for (const char* p = contents; (p = std::strstr(p, "frozen=")) != NULL; ++p) {
      frozen_line_count++;
    }
    check(frozen_line_count == 1, "re-freezing does not duplicate the frozen= line");
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
