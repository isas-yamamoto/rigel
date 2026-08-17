#ifndef RIGEL_RIGEL_H_
#define RIGEL_RIGEL_H_

#include <sys/param.h>
#include <sys/types.h>
#include <list>
#include <mutex>
#include <unordered_map>
#include "google.h"

namespace rigel {

  // Library version (semantic versioning: major.minor.patch).
  const int VERSION_MAJOR = 1;
  const int VERSION_MINOR = 0;
  const int VERSION_PATCH = 0;
  const char VERSION[] = "1.0.0";

  // Max File Size(default 128MB)
  const int MAX_FILE_COUNT = 131072;

  // Max File index: 1024 x 1024
  //   for 128MB, 16384 x 128MB = 2TB
  const int MAX_FILE_INDEX =16384;

  const int BLOCK_SIZE = 1024;
  const int MAX_KEY_SIZE = 256;

  const int BUF_SIZE = 4096;

  // A single Rigel instance can be shared across threads: Write/Read/
  // ScanInit/ScanNext may be called concurrently (serialized internally by
  // one mutex). Concurrent writes from multiple *processes* to the same
  // directory are out of scope (no flock or other file locking is done).
  class Rigel {

 public:
    Rigel();
    virtual ~Rigel();

    // Initializes with dirname/key/block_size/max_file_count given directly.
    virtual void Init(const char* dirname,
                      const char* key,
                      const int block_size=BLOCK_SIZE,
                      const int max_file_count=MAX_FILE_COUNT);

    // Initializes by reading key/block_size/max_file_count from the
    // metadata file under dirname (written by WriteMeta). Returns false if
    // the metadata is missing or malformed.
    virtual bool Init(const char* dirname);

    // Writes key/block_size/max_file_count as metadata under dirname.
    // Normally called from the rigel_init command.
    static bool WriteMeta(const char* dirname,
                          const char* key,
                          const int block_size=BLOCK_SIZE,
                          const int max_file_count=MAX_FILE_COUNT);

    virtual ssize_t Write(const int index,
                          const unsigned char* data,
                          size_t size);

    virtual ssize_t Read(const int index,
                         unsigned char* data,
                         size_t size);

    virtual bool ScanInit(const int start=0);
    virtual int ScanNext();

    // Returns details of the most recent failure (including an
    // errno-derived message where relevant). Meant to be called right
    // after Write/Read/ScanInit/Init(dirname) returns a failure. Not set
    // for normal, expected failures such as reading an index that was
    // never written (only real I/O errors or misuse set it). In
    // multithreaded use, read it before another thread makes the next
    // call (one buffer is reused per instance).
    const char* LastError() const;

    // Accessors for the parameters given to Init(). Mainly useful for
    // tools (e.g. a CLI) that only know a directory and need to recover
    // the key/block_size/max_file_count that directory was set up with.
    int BlockSize() const { return this->block_size_; }
    const char* Key() const { return this->key_; }
    int MaxFileCount() const {
      return (this->block_size_ > 0)
          ? (int)(this->max_file_size_ / this->block_size_)
          : 0;
    }

 private:

    // A single data file (one per file_index, fixed at max_file_size_
    // bytes) mmap'd in full. Since the size is fixed by Init()'s
    // parameters, it never needs to grow once created; afterwards it's
    // just pointer arithmetic + memcpy.
    struct DataMapping {
      int fd;
      unsigned char* ptr;
      size_t size;
      DataMapping() : fd(-1), ptr(NULL), size(0) {}
    };

    // The index file (1 byte per index, a presence flag) mmap'd. Grows via
    // ftruncate + remap as needed, based on the highest index written.
    struct IndexMapping {
      int fd;
      unsigned char* ptr;
      size_t size;
      IndexMapping() : fd(-1), ptr(NULL), size(0) {}
    };

    int block_size_;
    unsigned long long max_file_size_;
    char dirname_[MAXPATHLEN];
    char key_[MAX_KEY_SIZE];

    // Bounded to MAX_OPEN_SHARDS entries (see rigel.cc) so a long-lived
    // process that touches many shards doesn't accumulate an unbounded
    // number of open file descriptors / mmap'd regions. lru_order_ holds
    // file_index values, most-recently-used at the front; lru_pos_ gives
    // O(1) access to each one's list node for move-to-front/erase.
    std::unordered_map<int, DataMapping> data_maps_;
    std::list<int> lru_order_;
    std::unordered_map<int, std::list<int>::iterator> lru_pos_;
    IndexMapping index_map_;

    // for Scan
    int scan_pos_;

    // Message for the most recent failure. Written by SetError(), read by
    // LastError().
    char last_error_[512];

    // Access to data_maps_/index_map_/scan_pos_/last_error_ is serialized
    // by this mutex. Coarse-grained by design: locked once at the entry of
    // Write/Read/ScanInit/ScanNext, so even unrelated shards never run
    // concurrently (prioritizes simple, clearly-correct thread safety over
    // concurrent throughput). Mutable so the const method LastError() can
    // lock it too.
    mutable std::mutex mutex_;

    void DataFilename(int file_index, char* buf, size_t buflen) const;
    void IndexFilename(char* buf, size_t buflen) const;

    // Callers must already hold mutex_ (this does not lock it itself; it's
    // only ever called from within Write/Read/ScanInit/Init(dirname),
    // which have already locked it).
    void SetError(const char* fmt, ...);

    DataMapping* GetDataMapping(int file_index);
    void TouchShard(int file_index);
    void EvictShardsIfNeeded();
    bool OpenIndexMapping();
    bool EnsureIndexSize(size_t min_size);

    DISALLOW_COPY_AND_ASSIGN(Rigel);
  };

} // namespace rigel

#endif  // RIGEL_RIGEL_H_
