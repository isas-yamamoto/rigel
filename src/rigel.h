#ifndef RIGEL_RIGEL_H_
#define RIGEL_RIGEL_H_

#include <sys/param.h>
#include <sys/types.h>
#include <fstream>
#include <unordered_map>
#include "google.h"

namespace rigel {

  // Max File Size(default 128MB)
  const int MAX_FILE_COUNT = 131072;

  // Max File index: 1024 x 1024
  //   for 128MB, 16384 x 128MB = 2TB
  const int MAX_FILE_INDEX =16384;

  const int BLOCK_SIZE = 1024;
  const int MAX_KEY_SIZE = 256;

  const int BUF_SIZE = 4096;

  enum OpType {
    kOpNone = 0,
    kOpRead = 1,
    kOpWrite = 2,
  };

  class Rigel {

 public:
    Rigel();

    virtual void Init(const char* dirname,
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

 private:

    // Read/Write 用のファイルハンドル。開いたら閉じず使い回し、
    // かつ直前の操作から位置が動いていなければ seek もスキップする。
    // (C++の規格上、read <-> write を切り替える際は間に seek が要るので、
    //  位置と直前操作の種別(last_op)の両方が一致したときだけ省略する)
    struct Handle {
      std::fstream stream;
      long long pos;   // -1: 位置不明(未使用/失敗直後)
      int last_op;     // kOpNone / kOpRead / kOpWrite
      Handle() : pos(-1), last_op(kOpNone) {}
    };

    int block_size_;
    unsigned long long max_file_size_;
    char dirname_[MAXPATHLEN];
    char key_[MAX_KEY_SIZE];

    std::unordered_map<int, Handle> data_handles_;
    Handle index_handle_;

    // for Scan
    std::fstream scan_io;
    char scan_buf_[BUF_SIZE];
    int scan_size_;
    int scan_index_;
    int scan_offset_;

    void DataFilename(int file_index, char* buf, size_t buflen) const;
    void IndexFilename(char* buf, size_t buflen) const;

    bool Open(const int index,
              OpType op,
              Handle** data_io,
              Handle** index_io);

    Handle* GetDataHandle(int file_index);
    Handle* GetIndexHandle();

    DISALLOW_COPY_AND_ASSIGN(Rigel);
  };

} // namespace rigel

#endif  // RIGEL_RIGEL_H_
