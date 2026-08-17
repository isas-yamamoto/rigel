/** @file
 *  @brief rigel.meta handling: Init(dirname), WriteMeta, SetFrozen, GetStat.
 *
 *  Split out of rigel.cc (which holds the mmap'd Write/Read/Delete/Scan
 *  core) purely to keep either file from growing without bound; both
 *  halves implement the same rigel::Rigel class declared in rigel.h.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "rigel.h"

namespace rigel
{

namespace {
const char META_FILENAME[] = "rigel.meta";

// key_ is interpolated directly into filesystem paths (DataFilename,
// IndexFilename: "dirname/key.NNNN", "dirname/key.index") with no other
// escaping. Restricting it to this allow-list - notably excluding '/' -
// guarantees the "key" portion can only ever be a single path component,
// so a key can never escape dirname via "../" traversal regardless of
// where it came from (a rigel.meta file is just as capable of holding an
// adversarial key as any other input, since it can be planted by anyone
// with write access to the directory, independent of whether dirname/the
// rest of the call came from a trusted caller).
bool IsValidKey(const char* key) {
  if (key[0] == '\0') {
    return false;
  }
  for (const char* p = key; *p != '\0'; ++p) {
    bool ok = (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// Shared by Init(dirname) and SetFrozen(): reads dirname/rigel.meta into
// the given fields. block_size/max_file_count default to BLOCK_SIZE/
// MAX_FILE_COUNT and index_offset/frozen default to 0/false if absent
// (metadata predating that field). Returns false (with errno set from the
// fopen failure) if the file doesn't exist.
bool ReadMetaFile(const char* dirname, char* key, size_t key_len,
                   int* block_size, int* max_file_count, int* index_offset,
                   bool* frozen) {
  char filename[MAXPATHLEN + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s", dirname, META_FILENAME);

  FILE* f = std::fopen(filename, "r");
  if (f == NULL) {
    return false;
  }

  key[0] = '\0';
  *block_size = BLOCK_SIZE;
  *max_file_count = MAX_FILE_COUNT;
  *index_offset = 0;
  *frozen = false;

  char line[512];
  while (std::fgets(line, sizeof(line), f) != NULL) {
    if (line[0] == '#') {
      continue; // comment line, even one containing "key=" or similar
    }
    char name[64];
    char value[448];
    if (std::sscanf(line, "%63[^=]=%447[^\n]", name, value) == 2) {
      if (std::strcmp(name, "key") == 0) {
        std::strncpy(key, value, key_len-1);
        key[key_len-1] = '\0';
      } else if (std::strcmp(name, "block_size") == 0) {
        *block_size = std::atoi(value);
      } else if (std::strcmp(name, "max_file_count") == 0) {
        *max_file_count = std::atoi(value);
      } else if (std::strcmp(name, "index_offset") == 0) {
        *index_offset = std::atoi(value);
      } else if (std::strcmp(name, "frozen") == 0) {
        *frozen = std::atoi(value) != 0;
      }
    }
  }
  std::fclose(f);
  return true;
}
}

/**
 *  @brief Reads the metadata file under dirname and initializes with the
 *  key/block_size/max_file_count found there.
 *
 *  @param[in] dirname directory name
 *  @param[in] read_only see the other Init() overload in rigel.h
 *  @return true if the metadata was read and initialization succeeded.
 *  false if the metadata is missing or malformed.
 */
bool Rigel::Init(const char* dirname, const bool read_only) {
  char key[MAX_KEY_SIZE];
  int block_size, max_file_count, index_offset;
  bool frozen;
  if (!ReadMetaFile(dirname, key, sizeof(key), &block_size, &max_file_count,
                     &index_offset, &frozen)) {
    this->SetError("Init: metadata not found (%s/%s): %s", dirname, META_FILENAME,
                    std::strerror(errno));
    return false;
  }

  if (!IsValidKey(key) || block_size <= 0 || max_file_count <= 0 || index_offset < 0) {
    this->SetError("Init: invalid metadata in %s/%s", dirname, META_FILENAME);
    return false;
  }

  this->Init(dirname, key, block_size, max_file_count, index_offset, read_only);
  this->frozen_ = frozen;
  return true;
}

/**
 *  @brief Writes key/block_size/max_file_count as metadata under dirname.
 *
 *  @return true on success.
 */
bool Rigel::WriteMeta(const char* dirname,
                      const char* key,
                      const int block_size,
                      const int max_file_count,
                      const int index_offset,
                      const bool frozen) {
  if (!IsValidKey(key)) {
    std::fprintf(stderr,
                  "Rigel::WriteMeta: invalid key \"%s\" (must be non-empty and contain only "
                  "letters, digits, '.', '_', '-')\n",
                  key);
    return false;
  }
  if (block_size <= 0 || max_file_count <= 0 || index_offset < 0) {
    // Init(dirname) already rejects block_size<=0/max_file_count<=0 read
    // back from rigel.meta (and rejecting index_offset<0 keeps Write/Read/
    // Delete's `index - index_offset_` from ever being computed with a
    // negative index_offset_, which risks signed overflow for large
    // indices) - catch the same problem here instead of writing a
    // rigel.meta that every future Init(dirname) will then refuse to read
    // (e.g. a mistyped, non-numeric CLI argument silently atoi()'d to 0).
    std::fprintf(stderr,
                  "Rigel::WriteMeta: invalid block_size=%d/max_file_count=%d/index_offset=%d "
                  "(block_size and max_file_count must be positive, index_offset must be "
                  "non-negative)\n",
                  block_size, max_file_count, index_offset);
    return false;
  }

  char filename[MAXPATHLEN + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s", dirname, META_FILENAME);

  FILE* f = std::fopen(filename, "w");
  if (f == NULL) {
    // Static method, so there's no instance to attach this to LastError() -
    // this is the one place that prints directly to stderr.
    std::fprintf(stderr, "Rigel::WriteMeta: fopen(%s) failed: %s\n",
                  filename, std::strerror(errno));
    return false;
  }
  std::fprintf(f, "key=%s\n", key);
  std::fprintf(f, "block_size=%d\n", block_size);
  std::fprintf(f, "max_file_count=%d\n", max_file_count);
  std::fprintf(f, "index_offset=%d\n", index_offset);
  std::fprintf(f, "frozen=%d\n", frozen ? 1 : 0);
  std::fclose(f);
  return true;
}

/**
 *  @brief Flips the frozen flag on an already-initialized directory in
 *  place, leaving key/block_size/max_file_count/index_offset - and any
 *  hand-added '#' comment lines - untouched.
 *
 *  Rewrites only the "frozen=" line (appending one if absent) rather than
 *  regenerating the whole file via WriteMeta(), which would silently drop
 *  comments a user had added to rigel.meta by hand.
 *
 *  @return false if dirname has no valid metadata to read.
 */
bool Rigel::SetFrozen(const char* dirname, bool frozen) {
  char key[MAX_KEY_SIZE];
  int block_size, max_file_count, index_offset;
  bool current_frozen; // unused; overwritten by the requested value below
  if (!ReadMetaFile(dirname, key, sizeof(key), &block_size, &max_file_count,
                     &index_offset, &current_frozen) ||
      !IsValidKey(key) || block_size <= 0 || max_file_count <= 0) {
    return false;
  }

  char filename[MAXPATHLEN + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s", dirname, META_FILENAME);

  FILE* in = std::fopen(filename, "r");
  if (in == NULL) {
    return false;
  }
  std::string rewritten;
  bool wrote_frozen_line = false;
  char line[512];
  while (std::fgets(line, sizeof(line), in) != NULL) {
    char name[64];
    char value[448];
    if (line[0] != '#' &&
        std::sscanf(line, "%63[^=]=%447[^\n]", name, value) == 2 &&
        std::strcmp(name, "frozen") == 0) {
      rewritten += frozen ? "frozen=1\n" : "frozen=0\n";
      wrote_frozen_line = true;
    } else {
      rewritten += line; // unrelated field, or a comment - kept as-is
    }
  }
  std::fclose(in);
  if (!wrote_frozen_line) {
    rewritten += frozen ? "frozen=1\n" : "frozen=0\n";
  }

  FILE* out = std::fopen(filename, "w");
  if (out == NULL) {
    return false;
  }
  std::fwrite(rewritten.data(), 1, rewritten.size(), out);
  std::fclose(out);
  return true;
}

bool Rigel::GetStat(Stat* out) {
  std::memset(out, 0, sizeof(*out));
  out->block_size = this->BlockSize();
  out->max_file_count = this->MaxFileCount();
  out->max_file_size = (unsigned long long)out->block_size * out->max_file_count;
  out->index_offset = this->IndexOffset();
  out->frozen = this->frozen_;
  out->min_index = -1;
  out->max_index = -1;

  if (!this->ScanInit(0)) {
    return false;
  }
  int idx;
  while ((idx = this->ScanNext()) >= 0) {
    out->record_count++;
    if (out->min_index < 0) {
      out->min_index = idx;
    }
    out->max_index = idx;
  }

  // Tally shard files (<key>.NNNN) and the index file on disk.
  std::string key_prefix = std::string(this->Key()) + ".";
  DIR* d = ::opendir(this->dirname_);
  if (d != NULL) {
    struct dirent* ent;
    while ((ent = ::readdir(d)) != NULL) {
      std::string name(ent->d_name);
      if (name.size() == key_prefix.size() + 4 &&
          name.compare(0, key_prefix.size(), key_prefix) == 0 &&
          name.find_first_not_of("0123456789", key_prefix.size()) == std::string::npos) {
        char path[MAXPATHLEN + MAX_KEY_SIZE + 32];
        std::snprintf(path, sizeof(path), "%s/%s", this->dirname_, name.c_str());
        struct stat st;
        if (::stat(path, &st) == 0) {
          out->shard_count++;
          out->shard_bytes += (unsigned long long)st.st_size;
        }
      }
    }
    ::closedir(d);
  }

  char index_path[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->IndexFilename(index_path, sizeof(index_path));
  struct stat ist;
  if (::stat(index_path, &ist) == 0) {
    out->index_bytes = (unsigned long long)ist.st_size;
  }

  return true;
}

} // namespace rigel
