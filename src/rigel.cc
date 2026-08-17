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

namespace {

std::ios_base::openmode ModeFromString(const char* mode) {
  if (std::strcmp(mode, "r+b") == 0) {
    return std::ios::in | std::ios::out | std::ios::binary;
  }
  if (std::strcmp(mode, "w+b") == 0) {
    return std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc;
  }
  // "rb" and anything else
  return std::ios::in | std::ios::binary;
}

} // namespace

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

/**
 *  @brief 関連ファイルを開く。
 *
 *  @param[in] data_io データ関連のファイルを扱うクラス
 *  @param[in] index_io インデックス関連のファイルを扱うクラス
 *  @param[in] mode ファイルを開くモード
 *  @param[in] err_mode modeでファイルを開けなかった場合に開くモード
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::Open(const int index,
                    std::fstream& data_io,
                    std::fstream& index_io,
                    const char* mode,
                    const char* err_mode) {

  unsigned long long offset = index;
  offset *= this->block_size_;

  int file_index  = offset / this->max_file_size_;
  int file_offset = offset % this->max_file_size_;

  if (file_index < 0 || file_index >= MAX_FILE_INDEX) {
    return false;
  }

  if (OpenData(data_io,file_index,mode,err_mode)) {
    data_io.seekg(file_offset, std::ios::beg);
    data_io.seekp(file_offset, std::ios::beg);
  } else {
    return false;
  }

  if (OpenIndex(index_io,mode,err_mode)) {
    index_io.seekg(index, std::ios::beg);
    index_io.seekp(index, std::ios::beg);
  } else {
    return false;
  }

  return true;
}

/**
 *  @brief データファイルを開く。
 *
 *  @param[in] data_io データ関連のファイルを扱うクラス
 *  @param[in] file_index 開くべきファイルのインデックス
 *  @param[in] mode ファイルを開くモード
 *  @param[in] err_mode modeでファイルを開けなかった場合に開くモード
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::OpenData(std::fstream& data_io,
                     int file_index,
                     const char* mode,
                     const char* err_mode) {
  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s.%04d",
                this->dirname_, this->key_, file_index);

  if (data_io.is_open()) {
    data_io.close();
  }
  data_io.clear();
  data_io.open(filename, ModeFromString(mode));
  if (!data_io.is_open()) {
    data_io.clear();
    data_io.open(filename, ModeFromString(err_mode));
    if (!data_io.is_open()) {
      std::fprintf(stderr, "ERROR Rigel::Open filename=%s\n", filename);
      return false;
    }
  }
  return true;
}

/**
 *  @brief インデックスファイルを開く。
 *
 *  @param[in] index_io インデックス関連のファイルを扱うクラス
 *  @param[in] mode ファイルを開くモード
 *  @param[in] err_mode modeでファイルを開けなかった場合に開くモード
 *  @return 成功したときはtrueを返す。
 *  失敗したときはfalseを返す。
 */
bool Rigel::OpenIndex(std::fstream& index_io,
                      const char* mode,
                      const char* err_mode) {
  char filename[MAXPATHLEN + MAX_KEY_SIZE + 32];
  std::snprintf(filename, sizeof(filename), "%s/%s.index",
                this->dirname_, this->key_);

  if (index_io.is_open()) {
    index_io.close();
  }
  index_io.clear();
  index_io.open(filename, ModeFromString(mode));
  if (!index_io.is_open()) {
    index_io.clear();
    index_io.open(filename, ModeFromString(err_mode));
    if (!index_io.is_open()) {
      return false;
    }
  }
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
  std::fstream data_io, index_io;
  if(this->Open(index, data_io, index_io, "r+b", "w+b")) {
    data_io.write(reinterpret_cast<const char*>(data), size);
    ssize_t ret = data_io.fail() ? -1 : (ssize_t)size;
    char c = 1;
    index_io.write(&c,1);
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
  std::fstream data_io, index_io;
  if (this->Open(index, data_io, index_io, "rb", "rb")) {
    char c = 0;
    index_io.read(&c,1);
    if (index_io.gcount() == 1 && c == 1) {
      data_io.read(reinterpret_cast<char*>(data), size);
      std::streamsize n = data_io.gcount();
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
  if (this->OpenIndex(this->scan_io,"rb","rb")) {
    this->scan_io.seekg(0, std::ios::beg);
    this->scan_index_ = 0;
    this->scan_offset_ = 0;
    this->scan_io.read(this->scan_buf_, BUF_SIZE);
    this->scan_size_ = (int)this->scan_io.gcount();
    return true;
  }
  return false;
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
