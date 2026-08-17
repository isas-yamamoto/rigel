#ifndef RIGEL_RIGEL_H_
#define RIGEL_RIGEL_H_

#include <sys/param.h>
#include <sys/types.h>
#include <fstream>
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

    int block_size_;
    unsigned long long max_file_size_;
    char dirname_[MAXPATHLEN];
    char key_[MAX_KEY_SIZE];

    // for Scan
    std::fstream scan_io;
    char scan_buf_[BUF_SIZE];
    int scan_size_;
    int scan_index_;
    int scan_offset_;

    bool Open(const int index,
                 std::fstream& data_io,
                 std::fstream& index_io,
                 const char* mode,
                 const char* err_mode);

    bool OpenData(std::fstream& data_io,
                  int file_index,
                  const char* mode,
                  const char* err_mode);

    bool OpenIndex(std::fstream& index_io,
                   const char* mode,
                   const char* err_mode);

    DISALLOW_COPY_AND_ASSIGN(Rigel);
  };

} // namespace rigel

#endif  // RIGEL_RIGEL_H_
