/**
 * rigel_init: writes metadata (key/block_size/max_file_count) into a Rigel
 * data directory.
 *
 * Usage: rigel_init <dirname> <key> [block_size] [max_file_count]
 *
 * Re-running init on a directory that already has metadata, with a
 * different block_size/max_file_count, would make Read()'s offset math
 * disagree with the data already written there and corrupt reads - so
 * this refuses to overwrite an existing directory's metadata.
 */
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include "rigel.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s <dirname> <key> [block_size] [max_file_count]\n", argv[0]);
    return 1;
  }

  const char* dirname = argv[1];
  const char* key = argv[2];
  int block_size = (argc > 3) ? std::atoi(argv[3]) : rigel::BLOCK_SIZE;
  int max_file_count = (argc > 4) ? std::atoi(argv[4]) : rigel::MAX_FILE_COUNT;

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

  if (!rigel::Rigel::WriteMeta(dirname, key, block_size, max_file_count)) {
    // WriteMeta itself already prints the failure reason to stderr.
    return 1;
  }

  std::printf("initialized %s (key=%s block_size=%d max_file_count=%d)\n",
              dirname, key, block_size, max_file_count);
  return 0;
}
