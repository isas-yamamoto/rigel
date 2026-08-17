/** @file
 *  @brief Implementation of the Rigel class
 *  @author Yukio Yamamoto
 *  @since November 6, 2009
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rigel.h"

namespace rigel
{

namespace {
const size_t INDEX_GROW_CHUNK = 1 << 20; // grow the index file 1MiB at a time

// Caps how many data shards can be mmap'd/open at once. Without this, a
// long-lived process that touches many distinct shards over its lifetime
// would accumulate one open fd + one mmap'd region per shard forever,
// eventually hitting RLIMIT_NOFILE or running out of address space.
// Evicting the least-recently-used shard when this is exceeded keeps
// resource use bounded regardless of how many shards get touched overall;
// the cost is that a shard evicted and then touched again has to be
// reopened (one extra open+ftruncate+mmap, same as a first touch).
//
// A flat 1024 broke on hosts where RLIMIT_NOFILE is at or below that (a
// common default) - the index fd, stdio, and whatever fds the calling
// process already holds pushed it over, so open() started failing with
// EMFILE partway through. Scale the cap to the process's actual fd
// budget instead, leaving headroom for those. Computed once (RLIMIT_NOFILE
// doesn't change at runtime) and shared by every Rigel instance in the
// process, since the limit itself is process-wide.
size_t MaxOpenShards() {
  static const size_t cap = [] {
    const size_t kDefaultCap = 1024;
    const size_t kHeadroom = 64;   // stdio, the index fd, caller's own fds
    const size_t kMinCap = 8;
    struct rlimit rl;
    if (::getrlimit(RLIMIT_NOFILE, &rl) != 0) {
      return kDefaultCap; // couldn't query; keep the old assumption
    }
    if (rl.rlim_cur <= kHeadroom) {
      return kMinCap;
    }
    size_t budget = (size_t)rl.rlim_cur - kHeadroom;
    return (budget < kDefaultCap) ? budget : kDefaultCap;
  }();
  return cap;
}
}

/**
 *  @brief constructor
 *
 */
Rigel::Rigel()
  : block_size_(0),
    max_file_size_(0),
    index_offset_(0),
    frozen_(false),
    scan_pos_(0) {
  this->dirname_[0] = '\0';
  this->key_[0] = '\0';
  this->last_error_[0] = '\0';
}

/**
 *  @brief destructor
 *
 *  Releases every mmap'd region and file descriptor.
 */
Rigel::~Rigel() {
  for (std::unordered_map<int, DataMapping>::iterator it = this->data_maps_.begin();
       it != this->data_maps_.end(); ++it) {
    if (it->second.ptr != NULL) {
      ::munmap(it->second.ptr, it->second.size);
    }
    if (it->second.fd >= 0) {
      ::close(it->second.fd);
    }
  }
  if (this->index_map_.ptr != NULL) {
    ::munmap(this->index_map_.ptr, this->index_map_.size);
  }
  if (this->index_map_.fd >= 0) {
    ::close(this->index_map_.fd);
  }
}

/**
 *  @brief Initializes the various parameters.
 *
 *  @param[in] dirname directory name
 *  @param[in] key key string
 *  @param[in] block_size block size
 *  @param[in] max_file_count max number of blocks per file
 */
void Rigel::Init(const char* dirname,
                 const char* key,
                 const int block_size,
                 const int max_file_count,
                 const int index_offset) {
  this->block_size_ = block_size;
  this->max_file_size_ = (unsigned long long)max_file_count * block_size;
  this->index_offset_ = index_offset;
  this->frozen_ = false;

  std::strncpy(this->dirname_, dirname, MAXPATHLEN-1);
  this->dirname_[MAXPATHLEN-1] = '\0';

  std::strncpy(this->key_, key, MAX_KEY_SIZE-1);
  this->key_[MAX_KEY_SIZE-1] = '\0';
}

/**
 *  @brief Records the details of the most recent failure into last_error_
 *  (printf-style).
 *
 *  Assumes the caller already holds mutex_; this does not lock it again.
 */
void Rigel::SetError(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(this->last_error_, sizeof(this->last_error_), fmt, ap);
  va_end(ap);
}

/**
 *  @brief Returns the details of the most recent failure.
 */
const char* Rigel::LastError() const {
  std::lock_guard<std::mutex> lock(this->mutex_);
  return this->last_error_;
}

void Rigel::DataFilename(int file_index, char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.%04d", this->dirname_, this->key_, file_index);
}

void Rigel::IndexFilename(char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.index", this->dirname_, this->key_);
}

/**
 *  @brief Returns the mmap'd data file corresponding to file_index.
 *
 *  A single data file's size is fixed at max_file_size_, so once created it
 *  never needs to grow (mmap itself guards against out-of-bounds access).
 *
 *  @return a pointer to the mapping on success, nullptr on failure.
 */
Rigel::DataMapping* Rigel::GetDataMapping(int file_index) {
  std::unordered_map<int, DataMapping>::iterator it = this->data_maps_.find(file_index);
  if (it != this->data_maps_.end()) {
    this->TouchShard(file_index);
    return &it->second;
  }

  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->DataFilename(file_index, filename, sizeof(filename));

  int fd = ::open(filename, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    this->SetError("open(%s) failed: %s", filename, std::strerror(errno));
    return NULL;
  }

  struct stat st;
  if (::fstat(fd, &st) != 0) {
    this->SetError("fstat(%s) failed: %s", filename, std::strerror(errno));
    ::close(fd);
    return NULL;
  }
  if ((unsigned long long)st.st_size < this->max_file_size_) {
    if (::ftruncate(fd, (off_t)this->max_file_size_) != 0) {
      this->SetError("ftruncate(%s, %llu) failed: %s",
                      filename, this->max_file_size_, std::strerror(errno));
      ::close(fd);
      return NULL;
    }
  }

  void* p = ::mmap(NULL, this->max_file_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    this->SetError("mmap(%s, %llu bytes) failed: %s",
                    filename, this->max_file_size_, std::strerror(errno));
    ::close(fd);
    return NULL;
  }

  DataMapping m;
  m.fd = fd;
  m.ptr = static_cast<unsigned char*>(p);
  m.size = this->max_file_size_;

  std::pair<std::unordered_map<int, DataMapping>::iterator, bool> res =
      this->data_maps_.insert(std::make_pair(file_index, m));
  this->TouchShard(file_index);
  this->EvictShardsIfNeeded();
  return &res.first->second;
}

/**
 *  @brief Marks file_index as the most recently used shard.
 */
void Rigel::TouchShard(int file_index) {
  std::unordered_map<int, std::list<int>::iterator>::iterator pit =
      this->lru_pos_.find(file_index);
  if (pit != this->lru_pos_.end()) {
    this->lru_order_.erase(pit->second);
  }
  this->lru_order_.push_front(file_index);
  this->lru_pos_[file_index] = this->lru_order_.begin();
}

/**
 *  @brief Closes and unmaps the least-recently-used shards until at most
 *  MaxOpenShards() remain open.
 */
void Rigel::EvictShardsIfNeeded() {
  while (this->data_maps_.size() > MaxOpenShards() && !this->lru_order_.empty()) {
    int victim = this->lru_order_.back();
    this->lru_order_.pop_back();
    this->lru_pos_.erase(victim);

    std::unordered_map<int, DataMapping>::iterator it = this->data_maps_.find(victim);
    if (it != this->data_maps_.end()) {
      if (it->second.ptr != NULL) {
        ::munmap(it->second.ptr, it->second.size);
      }
      if (it->second.fd >= 0) {
        ::close(it->second.fd);
      }
      this->data_maps_.erase(it);
    }
  }
}

/**
 *  @brief Opens the index file if it isn't open yet.
 *
 *  If another process has already written to the index, maps it at that
 *  file's existing size right away.
 *
 *  @return true on success.
 */
bool Rigel::OpenIndexMapping() {
  if (this->index_map_.fd >= 0) {
    return true;
  }

  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->IndexFilename(filename, sizeof(filename));

  int fd = ::open(filename, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    this->SetError("open(%s) failed: %s", filename, std::strerror(errno));
    return false;
  }
  this->index_map_.fd = fd;

  struct stat st;
  st.st_size = 0;
  if (::fstat(fd, &st) != 0) {
    this->SetError("fstat(%s) failed: %s", filename, std::strerror(errno));
    // Not fatal - proceed as if this were a fresh, empty file.
  }
  if (st.st_size > 0) {
    if (!this->EnsureIndexSize((size_t)st.st_size)) {
      ::close(fd);
      this->index_map_.fd = -1;
      return false;
    }
  }
  return true;
}

/**
 *  @brief Ensures the index file's mmap region is at least min_size bytes.
 *
 *  Does nothing if it's already big enough. Otherwise grows the file with
 *  ftruncate and remaps it.
 *
 *  @return true on success.
 */
bool Rigel::EnsureIndexSize(size_t min_size) {
  if (this->index_map_.size >= min_size) {
    return true;
  }

  size_t chunks = (min_size + INDEX_GROW_CHUNK - 1) / INDEX_GROW_CHUNK;
  size_t new_size = chunks * INDEX_GROW_CHUNK;

  if (::ftruncate(this->index_map_.fd, (off_t)new_size) != 0) {
    this->SetError("ftruncate(index, %zu bytes) failed: %s", new_size, std::strerror(errno));
    return false;
  }

  if (this->index_map_.ptr != NULL) {
    ::munmap(this->index_map_.ptr, this->index_map_.size);
    this->index_map_.ptr = NULL;
    this->index_map_.size = 0;
  }

  void* p = ::mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, this->index_map_.fd, 0);
  if (p == MAP_FAILED) {
    this->SetError("mmap(index, %zu bytes) failed: %s", new_size, std::strerror(errno));
    return false;
  }

  this->index_map_.ptr = static_cast<unsigned char*>(p);
  this->index_map_.size = new_size;
  return true;
}

/**
 *  @brief Writes data at the position given by index.
 *
 *  @param[in] index index
 *  @param[in] data data to write
 *  @param[in] size size of the data to write
 *  @return the number of bytes written on success.
 *  -1 on failure.
 */
ssize_t Rigel::Write(const int index,
                     const unsigned char* data,
                     size_t size) {
  std::lock_guard<std::mutex> lock(this->mutex_);

  if (this->frozen_) {
    this->SetError("Write: directory is frozen (see Rigel::SetFrozen)");
    return -1;
  }

  int idx = index - this->index_offset_;
  if (idx < 0) {
    this->SetError("Write: index %d is below index_offset %d", index, this->index_offset_);
    return -1;
  }

  unsigned long long offset = (unsigned long long)idx * this->block_size_;
  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    this->SetError("Write: index %d out of range (file_index=%d, MAX_FILE_INDEX=%d)",
                    index, file_index, MAX_FILE_INDEX);
    return -1;
  }

  DataMapping* dm = this->GetDataMapping(file_index);
  if (dm == NULL) {
    return -1;
  }
  if ((size_t)file_offset + size > dm->size) {
    this->SetError("Write: size %zu at offset %d exceeds shard size %zu (index=%d)",
                    size, file_offset, dm->size, index);
    return -1;
  }
  std::memcpy(dm->ptr + file_offset, data, size);
  if (size < (size_t)this->block_size_) {
    std::memset(dm->ptr + file_offset + size, 0, this->block_size_ - size);
  }

  if (!this->OpenIndexMapping()) {
    return -1;
  }
  if (!this->EnsureIndexSize((size_t)idx + 1)) {
    return -1;
  }
  this->index_map_.ptr[idx] = 1;

  return (ssize_t)size;
}

/**
 *  @brief Clears index so it reads as never-written again.
 *
 *  @param[in] index index
 *  @return true on success (including when index was never written).
 *  false on failure.
 */
bool Rigel::Delete(const int index) {
  std::lock_guard<std::mutex> lock(this->mutex_);

  if (this->frozen_) {
    this->SetError("Delete: directory is frozen (see Rigel::SetFrozen)");
    return false;
  }

  int idx = index - this->index_offset_;

  if (!this->OpenIndexMapping()) {
    return false;
  }
  if (idx < 0 || (size_t)idx >= this->index_map_.size ||
      this->index_map_.ptr[idx] != 1) {
    return true; // never written; nothing to do
  }

  unsigned long long offset = (unsigned long long)idx * this->block_size_;
  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  DataMapping* dm = this->GetDataMapping(file_index);
  if (dm == NULL) {
    return false;
  }
  std::memset(dm->ptr + file_offset, 0, this->block_size_);

  this->index_map_.ptr[idx] = 0;
  return true;
}

/**
 *  @brief Reads data from the position given by index.
 *
 *  @param[in] index index
 *  @param[in] data buffer to read into
 *  @param[in] size size to read
 *  @return the number of bytes read on success.
 *  -1 on failure.
 */
ssize_t Rigel::Read(const int index,
                    unsigned char* data,
                    size_t size) {
  std::lock_guard<std::mutex> lock(this->mutex_);

  int idx = index - this->index_offset_;
  if (idx < 0) {
    return -1; // below index_offset: never written (normal, not an error)
  }

  unsigned long long offset = (unsigned long long)idx * this->block_size_;
  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    this->SetError("Read: index %d out of range (file_index=%d, MAX_FILE_INDEX=%d)",
                    index, file_index, MAX_FILE_INDEX);
    return -1;
  }

  if (!this->OpenIndexMapping()) {
    return -1;
  }
  if ((size_t)idx >= this->index_map_.size) {
    return -1; // never written this far (normal, not an error)
  }
  if (this->index_map_.ptr[idx] != 1) {
    return -1;
  }

  DataMapping* dm = this->GetDataMapping(file_index);
  if (dm == NULL) {
    return -1;
  }
  size_t avail = dm->size - (size_t)file_offset;
  size_t n = (size < avail) ? size : avail;
  std::memcpy(data, dm->ptr + file_offset, n);
  return (ssize_t)n;
}

/**
 *  @brief Initializes a scan.
 *
 *  @return true on success.
 *  false on failure.
 */
bool Rigel::ScanInit(const int start) {
  std::lock_guard<std::mutex> lock(this->mutex_);

  if (!this->OpenIndexMapping()) {
    return false;
  }
  int internal_start = start - this->index_offset_;
  this->scan_pos_ = (internal_start > 0) ? internal_start : 0;
  return true;
}

/**
 *  @brief Finds the next written index and returns it.
 *
 *  @return the index on success.
 *  -1 on failure (nothing left to scan).
 */
int Rigel::ScanNext() {
  std::lock_guard<std::mutex> lock(this->mutex_);

  if (this->index_map_.ptr == NULL) {
    return -1;
  }
  while ((size_t)this->scan_pos_ < this->index_map_.size) {
    int idx = this->scan_pos_;
    this->scan_pos_++;
    if (this->index_map_.ptr[idx] != 0) {
      return idx + this->index_offset_;
    }
  }
  return -1;
}

} // name space rigel
