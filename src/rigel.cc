/** @file
 *  @brief Rigelクラスの実装部
 *  @author Yukio Yamamoto
 *  @date November 6, 2009 version 0.01
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "rigel.h"

namespace rigel
{

namespace {
const size_t INDEX_GROW_CHUNK = 1 << 20; // 1MiB分ずつ伸長
const char META_FILENAME[] = "rigel.meta";
}

/**
 *  @brief constructor
 *
 */
Rigel::Rigel()
  : block_size_(0),
    max_file_size_(0),
    scan_pos_(0) {
  this->dirname_[0] = '\0';
  this->key_[0] = '\0';
}

/**
 *  @brief destructor
 *
 *  mmapしていた領域とファイルディスクリプタを全て解放する。
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
 *  @brief 各種パラメータを初期化する。
 *
 *  @param[in] dirname ディレクトリ名称
 *  @param[in] key キーワード
 *  @param[in] block_size ブロックサイズ
 *  @param[in] max_file_count 最大ファイル数
 */
void Rigel::Init(const char* dirname,
                 const char* key,
                 const int block_size,
                 const int max_file_count) {
  this->block_size_ = block_size;
  this->max_file_size_ = (unsigned long long)max_file_count * block_size;

  std::strncpy(this->dirname_, dirname, MAXPATHLEN-1);
  this->dirname_[MAXPATHLEN-1] = '\0';

  std::strncpy(this->key_, key, MAX_KEY_SIZE-1);
  this->key_[MAX_KEY_SIZE-1] = '\0';
}

/**
 *  @brief dirname配下のメタデータファイルを読み、そこに書かれたkey/block_size/
 *  max_file_countで初期化する。
 *
 *  @param[in] dirname ディレクトリ名称
 *  @return メタデータが正しく読めて初期化できたときはtrueを返す。
 *  メタデータが無い/壊れているときはfalseを返す。
 */
bool Rigel::Init(const char* dirname) {
  char filename[MAXPATHLEN + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s", dirname, META_FILENAME);

  FILE* f = std::fopen(filename, "r");
  if (f == NULL) {
    std::fprintf(stderr, "ERROR Rigel::Init metadata not found: %s\n", filename);
    return false;
  }

  char key[MAX_KEY_SIZE];
  key[0] = '\0';
  int block_size = BLOCK_SIZE;
  int max_file_count = MAX_FILE_COUNT;

  char line[512];
  while (std::fgets(line, sizeof(line), f) != NULL) {
    char name[64];
    char value[448];
    if (std::sscanf(line, "%63[^=]=%447[^\n]", name, value) == 2) {
      if (std::strcmp(name, "key") == 0) {
        std::strncpy(key, value, MAX_KEY_SIZE-1);
        key[MAX_KEY_SIZE-1] = '\0';
      } else if (std::strcmp(name, "block_size") == 0) {
        block_size = std::atoi(value);
      } else if (std::strcmp(name, "max_file_count") == 0) {
        max_file_count = std::atoi(value);
      }
    }
  }
  std::fclose(f);

  if (key[0] == '\0' || block_size <= 0 || max_file_count <= 0) {
    std::fprintf(stderr, "ERROR Rigel::Init invalid metadata: %s\n", filename);
    return false;
  }

  this->Init(dirname, key, block_size, max_file_count);
  return true;
}

/**
 *  @brief dirname配下にkey/block_size/max_file_countをメタデータとして書き込む。
 *
 *  @return 成功したときはtrueを返す。
 */
bool Rigel::WriteMeta(const char* dirname,
                      const char* key,
                      const int block_size,
                      const int max_file_count) {
  char filename[MAXPATHLEN + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s", dirname, META_FILENAME);

  FILE* f = std::fopen(filename, "w");
  if (f == NULL) {
    return false;
  }
  std::fprintf(f, "key=%s\n", key);
  std::fprintf(f, "block_size=%d\n", block_size);
  std::fprintf(f, "max_file_count=%d\n", max_file_count);
  std::fclose(f);
  return true;
}

void Rigel::DataFilename(int file_index, char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.%04d", this->dirname_, this->key_, file_index);
}

void Rigel::IndexFilename(char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.index", this->dirname_, this->key_);
}

/**
 *  @brief file_indexに対応するデータファイルをmmapしたものを返す。
 *
 *  データファイル1つの大きさは max_file_size_ で固定なので、
 *  一度確保したら伸長は不要（範囲外アクセスはmmap自体が守ってくれる）。
 *
 *  @return 成功したときはマッピングへのポインタ、失敗したときはnullptr。
 */
Rigel::DataMapping* Rigel::GetDataMapping(int file_index) {
  std::unordered_map<int, DataMapping>::iterator it = this->data_maps_.find(file_index);
  if (it != this->data_maps_.end()) {
    return &it->second;
  }

  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->DataFilename(file_index, filename, sizeof(filename));

  int fd = ::open(filename, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    std::fprintf(stderr, "ERROR Rigel::Open filename=%s\n", filename);
    return NULL;
  }

  struct stat st;
  if (::fstat(fd, &st) != 0) {
    ::close(fd);
    return NULL;
  }
  if ((unsigned long long)st.st_size < this->max_file_size_) {
    if (::ftruncate(fd, (off_t)this->max_file_size_) != 0) {
      ::close(fd);
      return NULL;
    }
  }

  void* p = ::mmap(NULL, this->max_file_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    ::close(fd);
    return NULL;
  }

  DataMapping m;
  m.fd = fd;
  m.ptr = (unsigned char*)p;
  m.size = this->max_file_size_;

  std::pair<std::unordered_map<int, DataMapping>::iterator, bool> res =
      this->data_maps_.insert(std::make_pair(file_index, m));
  return &res.first->second;
}

/**
 *  @brief インデックスファイルを（未オープンなら）開く。
 *
 *  既に他のプロセス等がindexを書き込んでいた場合は、そのファイルサイズ分だけ
 *  最初からmmapしておく。
 *
 *  @return 成功したときはtrueを返す。
 */
bool Rigel::OpenIndexMapping() {
  if (this->index_map_.fd >= 0) {
    return true;
  }

  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->IndexFilename(filename, sizeof(filename));

  int fd = ::open(filename, O_RDWR | O_CREAT, 0644);
  if (fd < 0) {
    return false;
  }
  this->index_map_.fd = fd;

  struct stat st;
  if (::fstat(fd, &st) == 0 && st.st_size > 0) {
    if (!this->EnsureIndexSize((size_t)st.st_size)) {
      ::close(fd);
      this->index_map_.fd = -1;
      return false;
    }
  }
  return true;
}

/**
 *  @brief インデックスファイルのmmap領域が少なくともmin_sizeバイトになるようにする。
 *
 *  既に十分な大きさがあれば何もしない。足りない場合はftruncateで伸長してから
 *  再mmapする。
 *
 *  @return 成功したときはtrueを返す。
 */
bool Rigel::EnsureIndexSize(size_t min_size) {
  if (this->index_map_.size >= min_size) {
    return true;
  }

  size_t chunks = (min_size + INDEX_GROW_CHUNK - 1) / INDEX_GROW_CHUNK;
  size_t new_size = chunks * INDEX_GROW_CHUNK;

  if (::ftruncate(this->index_map_.fd, (off_t)new_size) != 0) {
    return false;
  }

  if (this->index_map_.ptr != NULL) {
    ::munmap(this->index_map_.ptr, this->index_map_.size);
    this->index_map_.ptr = NULL;
    this->index_map_.size = 0;
  }

  void* p = ::mmap(NULL, new_size, PROT_READ | PROT_WRITE, MAP_SHARED, this->index_map_.fd, 0);
  if (p == MAP_FAILED) {
    return false;
  }

  this->index_map_.ptr = (unsigned char*)p;
  this->index_map_.size = new_size;
  return true;
}

/**
 *  @brief indexで指定した位置にデータを書き込む
 *
 *  @param[in] index インデックス
 *  @param[in] data 書き込むデータ
 *  @param[in] size 書き込むデータのサイズ
 *  @return 成功したときは書き込んだサイズを返す。
 *  失敗したときは-1を返す。
 */
ssize_t Rigel::Write(const int index,
                     const unsigned char* data,
                     size_t size) {
  unsigned long long offset = (unsigned long long)index * this->block_size_;
  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    return -1;
  }

  DataMapping* dm = this->GetDataMapping(file_index);
  if (dm == NULL) {
    return -1;
  }
  if ((size_t)file_offset + size > dm->size) {
    return -1;
  }
  std::memcpy(dm->ptr + file_offset, data, size);

  if (!this->OpenIndexMapping()) {
    return -1;
  }
  if (!this->EnsureIndexSize((size_t)index + 1)) {
    return -1;
  }
  this->index_map_.ptr[index] = 1;

  return (ssize_t)size;
}

/**
 *  @brief indexで指定した位置からデータを読み込む
 *
 *  @param[in] index インデックス
 *  @param[in] data 読み込んだデータ
 *  @param[in] size 読み込んだデータのサイズ
 *  @return 成功したときは読み込んだサイズを返す。
 *  失敗したときは-1を返す。
 */
ssize_t Rigel::Read(const int index,
                    unsigned char* data,
                    size_t size) {
  unsigned long long offset = (unsigned long long)index * this->block_size_;
  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    return -1;
  }

  if (!this->OpenIndexMapping()) {
    return -1;
  }
  if (index < 0 || (size_t)index >= this->index_map_.size) {
    return -1; // ここまで一度も書き込まれていない
  }
  if (this->index_map_.ptr[index] != 1) {
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
 *  @brief スキャンの初期化
 *
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::ScanInit(const int start) {
  if (!this->OpenIndexMapping()) {
    return false;
  }
  this->scan_pos_ = 0;
  return true;
}

/**
 *  @brief 次の候補を探し見つかったらindexを返す。
 *
 *  @return 成功したときはindexを返す。
 *  失敗したときは-1を返す。
 */
int Rigel::ScanNext() {
  if (this->index_map_.ptr == NULL) {
    return -1;
  }
  while ((size_t)this->scan_pos_ < this->index_map_.size) {
    int idx = this->scan_pos_;
    this->scan_pos_++;
    if (this->index_map_.ptr[idx] != 0) {
      return idx;
    }
  }
  return -1;
}

} // name space rigel
