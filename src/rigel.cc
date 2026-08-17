/** @file
 *  @brief Rigelクラスの実装部
 *  @author Yukio Yamamoto
 *  @date November 6, 2009 version 0.01
 */
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include "rigel.h"

namespace rigel
{

/**
 *  @brief constructor
 *
 */
Rigel::Rigel()
  : block_size_(0),
    max_file_size_(0) {
  this->dirname_[0] = '\0';
  this->key_[0] = '\0';
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

void Rigel::DataFilename(int file_index, char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.%04d", this->dirname_, this->key_, file_index);
}

void Rigel::IndexFilename(char* buf, size_t buflen) const {
  std::snprintf(buf, buflen, "%s/%s.index", this->dirname_, this->key_);
}

/**
 *  @brief file_indexに対応するデータファイルのハンドルを返す。
 *
 *  一度開いたハンドルはRigelインスタンスが破棄されるまで開いたままにする。
 *  毎回のRead/Writeでopen/closeするコストを避けるため。
 *
 *  @return 成功したときはハンドルへのポインタ、失敗したときはnullptr。
 */
Rigel::Handle* Rigel::GetDataHandle(int file_index) {
  Handle& h = this->data_handles_[file_index];
  if (!h.stream.is_open()) {
    char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
    this->DataFilename(file_index, filename, sizeof(filename));

    h.stream.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!h.stream.is_open()) {
      h.stream.clear();
      h.stream.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    }
    if (!h.stream.is_open()) {
      std::fprintf(stderr, "ERROR Rigel::Open filename=%s\n", filename);
      this->data_handles_.erase(file_index);
      return NULL;
    }
    h.pos = -1;
    h.last_op = kOpNone;
  }
  return &h;
}

/**
 *  @brief インデックスファイルのハンドルを返す（同様に開きっぱなしにする）。
 *
 *  @return 成功したときはハンドルへのポインタ、失敗したときはnullptr。
 */
Rigel::Handle* Rigel::GetIndexHandle() {
  if (!this->index_handle_.stream.is_open()) {
    char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
    this->IndexFilename(filename, sizeof(filename));

    this->index_handle_.stream.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!this->index_handle_.stream.is_open()) {
      this->index_handle_.stream.clear();
      this->index_handle_.stream.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    }
    if (!this->index_handle_.stream.is_open()) {
      return NULL;
    }
    this->index_handle_.pos = -1;
    this->index_handle_.last_op = kOpNone;
  }
  return &this->index_handle_;
}

/**
 *  @brief indexに対応するデータ/インデックスハンドルを求め、必要なら書き込み/読み込み位置までシークする。
 *
 *  ハンドルの現在位置が既に目的の位置にあり、かつ直前の操作が今回と同じ
 *  read/writeの向きであれば、seekそのものを省略する。
 *  (read/writeの向きが変わる場合は、C++の規格上シークを挟む必要があるため
 *   位置が同じでも必ずseekする)
 *
 *  @param[in] index インデックス
 *  @param[in] op 今回行う操作(kOpRead/kOpWrite)
 *  @param[out] data_io データファイルのハンドル
 *  @param[out] index_io インデックスファイルのハンドル
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::Open(const int index,
                    OpType op,
                    Handle** data_io,
                    Handle** index_io) {

  unsigned long long offset = index;
  offset *= this->block_size_;

  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    return false;
  }

  Handle* data = this->GetDataHandle(file_index);
  if (data == NULL) {
    return false;
  }
  if (data->pos != file_offset || data->last_op != op) {
    data->stream.clear();
    data->stream.seekg(file_offset, std::ios::beg);
    data->stream.seekp(file_offset, std::ios::beg);
  }
  data->pos = file_offset;
  data->last_op = op;

  Handle* idx = this->GetIndexHandle();
  if (idx == NULL) {
    return false;
  }
  if (idx->pos != index || idx->last_op != op) {
    idx->stream.clear();
    idx->stream.seekg(index, std::ios::beg);
    idx->stream.seekp(index, std::ios::beg);
  }
  idx->pos = index;
  idx->last_op = op;

  *data_io = data;
  *index_io = idx;
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
  Handle *data_io, *index_io;
  if (this->Open(index, kOpWrite, &data_io, &index_io)) {
    data_io->stream.write(reinterpret_cast<const char*>(data), size);
    bool ok = !data_io->stream.fail();
    data_io->stream.clear();
    data_io->pos = ok ? (data_io->pos + (long long)size) : -1;
    ssize_t ret = ok ? (ssize_t)size : -1;

    char c = 1;
    index_io->stream.write(&c,1);
    bool idx_ok = !index_io->stream.fail();
    index_io->stream.clear();
    index_io->pos = idx_ok ? (index_io->pos + 1) : -1;

    return ret;
  }
  return -1;
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
  Handle *data_io, *index_io;
  if (this->Open(index, kOpRead, &data_io, &index_io)) {
    char c = 0;
    index_io->stream.read(&c,1);
    bool index_ok = (index_io->stream.gcount() == 1);
    index_io->stream.clear();
    index_io->pos = index_ok ? (index_io->pos + 1) : -1;

    if (index_ok && c == 1) {
      data_io->stream.read(reinterpret_cast<char*>(data), size);
      std::streamsize n = data_io->stream.gcount();
      data_io->stream.clear();
      data_io->pos = (n > 0) ? (data_io->pos + n) : -1;
      return n > 0 ? (ssize_t)n : -1;
    }
  }
  return -1;
}

/**
 *  @brief スキャンの初期化
 *
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::ScanInit(const int start) {
  if (this->scan_io.is_open()) {
    this->scan_io.close();
  }
  this->scan_io.clear();

  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  this->IndexFilename(filename, sizeof(filename));
  this->scan_io.open(filename, std::ios::in | std::ios::binary);
  if (!this->scan_io.is_open()) {
    return false;
  }

  this->scan_io.seekg(0, std::ios::beg);
  this->scan_index_ = 0;
  this->scan_offset_ = 0;
  this->scan_io.read(this->scan_buf_, BUF_SIZE);
  this->scan_size_ = (int)this->scan_io.gcount();
  return true;
}

/**
 *  @brief 次の候補を探し見つかったらindexを返す。
 *
 *  @return 成功したときはindexを返す。
 *  失敗したときは-1を返す。
 */
int Rigel::ScanNext() {
  do {
    if(this->scan_index_ < this->scan_size_) {
      while (this->scan_index_ < this->scan_size_) {
        this->scan_index_++;
        if (this->scan_buf_[this->scan_index_-1] != 0) {
          return (this->scan_offset_ * BUF_SIZE) + this->scan_index_ - 1;
        }
      }
    }

    if (this->scan_index_ == this->scan_size_) {
      this->scan_io.read(this->scan_buf_, BUF_SIZE);
      this->scan_size_ = (int)this->scan_io.gcount();
      this->scan_index_ = 0;
      this->scan_offset_++;
    }
  } while (this->scan_size_ > 0);
  return -1;
}

} // name space rigel
