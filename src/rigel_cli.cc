/**
 * rigel: general-purpose command-line tool for a Rigel data directory.
 *
 * Usage:
 *   rigel init  <dir> <key> [block_size] [max_file_count] [index_offset]
 *   rigel read  <dir> <index> [--read-only]
 *                                         (writes the raw record to stdout)
 *   rigel write <dir> <index>            (reads the raw record from stdin)
 *   rigel delete <dir> <index>           (clears a record back to never-written)
 *   rigel scan  <dir> [start] [--read-only]
 *                                         (lists written indices, one per line)
 *   rigel dump  <dir> [start] [end] [--raw] [--read-only]
 *                                         (prints written records' content)
 *   rigel stat  <dir> [--read-only]      (prints key/geometry/usage info)
 *   rigel freeze <dir>                   (blocks further Write/Delete)
 *   rigel unfreeze <dir>                 (allows Write/Delete again)
 *   rigel version                        (prints the library version)
 *
 * Example: rigel read /data/foo 42 | hexdump -C
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <vector>

#include "rigel.h"

namespace {

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
      "usage: %s <command> [args]\n"
      "\n"
      "commands:\n"
      "  init  <dir> <key> [block_size] [max_file_count] [index_offset]\n"
      "  read  <dir> <index> [--read-only]\n"
      "                            write the raw record to stdout\n"
      "  write <dir> <index>       read the raw record from stdin\n"
      "  delete <dir> <index>      clear a record back to never-written\n"
      "  scan  <dir> [start] [--read-only]\n"
      "                            list written indices, one per line\n"
      "  dump  <dir> [start] [end] [--raw] [--read-only]\n"
      "                            print written records' content\n"
      "                            (default: \"index: hex\" one per line;\n"
      "                            --raw: concatenated raw bytes)\n"
      "  stat  <dir> [--read-only] print key/geometry/usage info\n"
      "  freeze <dir>              block further Write/Delete\n"
      "  unfreeze <dir>            allow Write/Delete again\n"
      "  version                   print the library version\n"
      "\n"
      "  --read-only (on read/scan/dump/stat): opens the directory with no\n"
      "  write access needed at all, so it works against files/mounts this\n"
      "  process can't write to (e.g. a read-only NFS export).\n",
      prog);
}

// Removes every argv entry equal to `flag`, setting *found if any were
// removed. argv[0] (the subcommand's own name, used in usage messages) is
// always kept. Lets a boolean flag appear anywhere after the positional
// args without a full argument-parsing library.
std::vector<char*> StripFlag(int argc, char** argv, const char* flag, bool* found) {
  *found = false;
  std::vector<char*> out;
  out.push_back(argv[0]);
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], flag) == 0) {
      *found = true;
    } else {
      out.push_back(argv[i]);
    }
  }
  return out;
}

int CmdVersion(int, char**) {
  std::printf("rigel %s\n", rigel::VERSION);
  return 0;
}

int CmdInit(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: rigel init <dir> <key> [block_size] [max_file_count] [index_offset]\n");
    return 1;
  }
  const char* dirname = argv[1];
  const char* key = argv[2];
  int block_size = (argc > 3) ? std::atoi(argv[3]) : rigel::BLOCK_SIZE;
  int max_file_count = (argc > 4) ? std::atoi(argv[4]) : rigel::MAX_FILE_COUNT;
  int index_offset = (argc > 5) ? std::atoi(argv[5]) : 0;

  ::mkdir(dirname, 0755);

  char meta_filename[1024];
  std::snprintf(meta_filename, sizeof(meta_filename), "%s/rigel.meta", dirname);

  struct stat st;
  if (::stat(meta_filename, &st) == 0) {
    std::fprintf(stderr,
                  "ERROR: %s already exists. Refusing to overwrite an existing data "
                  "directory's metadata (a different block_size/max_file_count would "
                  "corrupt reads of already-written data).\n",
                  meta_filename);
    return 1;
  }

  if (!rigel::Rigel::WriteMeta(dirname, key, block_size, max_file_count, index_offset)) {
    // WriteMeta itself already prints the failure reason to stderr.
    return 1;
  }

  std::printf("initialized %s (key=%s block_size=%d max_file_count=%d index_offset=%d)\n",
              dirname, key, block_size, max_file_count, index_offset);
  return 0;
}

int CmdRead(int argc, char** argv) {
  bool read_only;
  std::vector<char*> positional = StripFlag(argc, argv, "--read-only", &read_only);
  argc = (int)positional.size();
  argv = positional.data();

  if (argc < 3) {
    std::fprintf(stderr, "usage: rigel read <dir> <index> [--read-only]\n");
    return 1;
  }
  const char* dirname = argv[1];
  int index = std::atoi(argv[2]);

  rigel::Rigel rigel;
  if (!rigel.Init(dirname, read_only)) {
    std::fprintf(stderr, "rigel read: %s\n", rigel.LastError());
    return 1;
  }

  std::vector<unsigned char> buf(rigel.BlockSize());
  ssize_t n = rigel.Read(index, buf.data(), buf.size());
  if (n < 0) {
    std::fprintf(stderr, "rigel read: index %d not found\n", index);
    return 1;
  }

  std::fwrite(buf.data(), 1, (size_t)n, stdout);
  return 0;
}

int CmdWrite(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: rigel write <dir> <index>   (data comes from stdin)\n");
    return 1;
  }
  const char* dirname = argv[1];
  int index = std::atoi(argv[2]);

  rigel::Rigel rigel;
  if (!rigel.Init(dirname)) {
    std::fprintf(stderr, "rigel write: %s\n", rigel.LastError());
    return 1;
  }

  std::vector<unsigned char> buf(rigel.BlockSize());
  size_t n = std::fread(buf.data(), 1, buf.size(), stdin);

  ssize_t r = rigel.Write(index, buf.data(), n);
  if (r < 0) {
    std::fprintf(stderr, "rigel write: %s\n", rigel.LastError());
    return 1;
  }
  return 0;
}

int CmdDelete(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: rigel delete <dir> <index>\n");
    return 1;
  }
  const char* dirname = argv[1];
  int index = std::atoi(argv[2]);

  rigel::Rigel rigel;
  if (!rigel.Init(dirname)) {
    std::fprintf(stderr, "rigel delete: %s\n", rigel.LastError());
    return 1;
  }

  if (!rigel.Delete(index)) {
    std::fprintf(stderr, "rigel delete: %s\n", rigel.LastError());
    return 1;
  }
  return 0;
}

int CmdScan(int argc, char** argv) {
  bool read_only;
  std::vector<char*> positional = StripFlag(argc, argv, "--read-only", &read_only);
  argc = (int)positional.size();
  argv = positional.data();

  if (argc < 2) {
    std::fprintf(stderr, "usage: rigel scan <dir> [start] [--read-only]\n");
    return 1;
  }
  const char* dirname = argv[1];
  int start = (argc > 2) ? std::atoi(argv[2]) : 0;

  rigel::Rigel rigel;
  if (!rigel.Init(dirname, read_only)) {
    std::fprintf(stderr, "rigel scan: %s\n", rigel.LastError());
    return 1;
  }

  if (!rigel.ScanInit(start)) {
    std::fprintf(stderr, "rigel scan: %s\n", rigel.LastError());
    return 1;
  }
  int idx;
  while ((idx = rigel.ScanNext()) >= 0) {
    std::printf("%d\n", idx);
  }
  return 0;
}

int CmdDump(int argc, char** argv) {
  // --raw/--read-only can appear anywhere after <dir>; strip them out
  // before parsing the remaining args as positional start/end.
  bool raw, read_only;
  std::vector<char*> positional = StripFlag(argc, argv, "--raw", &raw);
  positional = StripFlag((int)positional.size(), positional.data(), "--read-only", &read_only);

  if (positional.size() < 2) {
    std::fprintf(stderr, "usage: rigel dump <dir> [start] [end] [--raw] [--read-only]\n");
    return 1;
  }

  const char* dirname = positional[1];
  int start = (positional.size() > 2) ? std::atoi(positional[2]) : 0;
  int end = (positional.size() > 3) ? std::atoi(positional[3]) : -1; // -1 = no upper bound

  rigel::Rigel rigel;
  if (!rigel.Init(dirname, read_only)) {
    std::fprintf(stderr, "rigel dump: %s\n", rigel.LastError());
    return 1;
  }

  if (!rigel.ScanInit(start)) {
    std::fprintf(stderr, "rigel dump: %s\n", rigel.LastError());
    return 1;
  }

  std::vector<unsigned char> buf(rigel.BlockSize());
  std::vector<char> hex(buf.size() * 2 + 1);
  int idx;
  while ((idx = rigel.ScanNext()) >= 0) {
    if (end >= 0 && idx > end) {
      break;
    }
    ssize_t n = rigel.Read(idx, buf.data(), buf.size());
    if (n < 0) {
      std::fprintf(stderr, "rigel dump: %s\n", rigel.LastError());
      return 1;
    }
    if (raw) {
      std::fwrite(buf.data(), 1, (size_t)n, stdout);
    } else {
      for (ssize_t i = 0; i < n; ++i) {
        std::snprintf(&hex[i * 2], 3, "%02x", buf[i]);
      }
      hex[n * 2] = '\0';
      std::printf("%d: %s\n", idx, hex.data());
    }
  }
  return 0;
}

int CmdStat(int argc, char** argv) {
  bool read_only;
  std::vector<char*> positional = StripFlag(argc, argv, "--read-only", &read_only);
  argc = (int)positional.size();
  argv = positional.data();

  if (argc < 2) {
    std::fprintf(stderr, "usage: rigel stat <dir> [--read-only]\n");
    return 1;
  }
  const char* dirname = argv[1];

  rigel::Rigel rigel;
  if (!rigel.Init(dirname, read_only)) {
    std::fprintf(stderr, "rigel stat: %s\n", rigel.LastError());
    return 1;
  }

  rigel::Rigel::Stat st;
  if (!rigel.GetStat(&st)) {
    std::fprintf(stderr, "rigel stat: %s\n", rigel.LastError());
    return 1;
  }

  std::printf("key:             %s\n", rigel.Key());
  std::printf("block_size:      %d\n", st.block_size);
  std::printf("max_file_count:  %d\n", st.max_file_count);
  std::printf("max_file_size:   %llu bytes\n", st.max_file_size);
  std::printf("index_offset:    %d\n", st.index_offset);
  std::printf("frozen:          %s\n", st.frozen ? "yes" : "no");
  std::printf("records:         %lld\n", st.record_count);
  if (st.record_count > 0) {
    std::printf("index range:     %d..%d\n", st.min_index, st.max_index);
  }
  std::printf("shard files:     %d\n", st.shard_count);
  std::printf("shard bytes:     %llu\n", st.shard_bytes);
  std::printf("index file bytes: %llu\n", st.index_bytes);

  return 0;
}

int CmdFreeze(int argc, char** argv, bool frozen) {
  const char* cmd_name = frozen ? "freeze" : "unfreeze";
  if (argc < 2) {
    std::fprintf(stderr, "usage: rigel %s <dir>\n", cmd_name);
    return 1;
  }
  const char* dirname = argv[1];

  if (!rigel::Rigel::SetFrozen(dirname, frozen)) {
    std::fprintf(stderr, "rigel %s: no valid metadata under %s\n", cmd_name, dirname);
    return 1;
  }
  std::printf("%s: %s\n", dirname, frozen ? "frozen" : "unfrozen");
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const char* cmd = argv[1];
  // Shift argv so subcommand handlers see argv[1] as their first real arg,
  // with argv[0] still naming the subcommand (for usage messages).
  int sub_argc = argc - 1;
  char** sub_argv = argv + 1;

  if (std::strcmp(cmd, "version") == 0 ||
      std::strcmp(cmd, "--version") == 0 ||
      std::strcmp(cmd, "-v") == 0) {
    return CmdVersion(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "init") == 0) {
    return CmdInit(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "read") == 0) {
    return CmdRead(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "write") == 0) {
    return CmdWrite(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "delete") == 0) {
    return CmdDelete(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "scan") == 0) {
    return CmdScan(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "dump") == 0) {
    return CmdDump(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "stat") == 0) {
    return CmdStat(sub_argc, sub_argv);
  }
  if (std::strcmp(cmd, "freeze") == 0) {
    return CmdFreeze(sub_argc, sub_argv, true);
  }
  if (std::strcmp(cmd, "unfreeze") == 0) {
    return CmdFreeze(sub_argc, sub_argv, false);
  }

  std::fprintf(stderr, "unknown command: %s\n\n", cmd);
  PrintUsage(argv[0]);
  return 1;
}
