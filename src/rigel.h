#ifndef RIGEL_RIGEL_H_
#define RIGEL_RIGEL_H_

#include <sys/param.h>
#include <sys/types.h>
#include <mutex>
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

  // 1つのRigelインスタンスをスレッド間で共有し、Write/Read/ScanInit/ScanNextを
  // 複数スレッドから同時に呼んでも安全（内部で1本のmutexにより直列化される）。
  // ただし複数「プロセス」から同じディレクトリへ同時に書き込む場合の排他は
  // 対象外（flock等のファイルロックは行っていない）。
  class Rigel {

 public:
    Rigel();
    virtual ~Rigel();

    // dirname/key/block_size/max_file_countを直接指定する版。
    virtual void Init(const char* dirname,
                      const char* key,
                      const int block_size=BLOCK_SIZE,
                      const int max_file_count=MAX_FILE_COUNT);

    // dirname配下のメタデータファイル(WriteMetaで書かれたもの)からkey/
    // block_size/max_file_countを読み込んで初期化する版。
    // メタデータが無い/壊れている場合はfalseを返す。
    virtual bool Init(const char* dirname);

    // key/block_size/max_file_countをdirname配下にメタデータとして書き込む。
    // 通常はrigel_initコマンドから呼ばれる。
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

    // 直近の失敗の詳細(errno由来のメッセージ含む)を返す。Write/Read/ScanInit/
    // Init(dirname)が失敗を返した直後に呼ぶことを想定している。
    // "書き込まれていないindexを読んだ"のような正常系の失敗ではセットされない
    // (実際のI/Oエラー・誤用のみが対象)。マルチスレッドで使う場合、他スレッドが
    // 次の呼び出しを行う前に読むこと（1インスタンスにつき1つのバッファを使い
    // 回している）。
    const char* LastError() const;

 private:

    // 1つのデータファイル(file_index毎、max_file_size_バイト固定長)をmmapしたもの。
    // サイズはInit()のパラメータで決まる固定値なので、一度作れば伸長は不要で
    // 以後はポインタ演算+memcpyのみになる。
    struct DataMapping {
      int fd;
      unsigned char* ptr;
      size_t size;
      DataMapping() : fd(-1), ptr(NULL), size(0) {}
    };

    // インデックスファイル(1byte/indexの存在フラグ)をmmapしたもの。
    // 書き込まれるindexの最大値に応じて伸長するため、必要になったら
    // ftruncate + 再mmapする。
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

    std::unordered_map<int, DataMapping> data_maps_;
    IndexMapping index_map_;

    // for Scan
    int scan_pos_;

    // 直近の失敗のメッセージ。SetError()で書き込み、LastError()で読む。
    char last_error_[512];

    // data_maps_/index_map_/scan_pos_/last_error_ へのアクセスはこのmutexで
    // 直列化する。Write/Read/ScanInit/ScanNextの入口で1本ロックする粗粒度な
    // 実装であり、異なるshardへの同時アクセスであっても並列には走らない
    // (スレッド安全性を単純かつ確実にすることを優先している)。
    // mutableなのはconstメソッドであるLastError()からもロックするため。
    mutable std::mutex mutex_;

    void DataFilename(int file_index, char* buf, size_t buflen) const;
    void IndexFilename(char* buf, size_t buflen) const;

    // 呼び出し側は必ずmutex_を保持した状態で呼ぶこと(自身ではロックしない。
    // Write/Read/ScanInit/ScanNextから既にロック済みの状態で呼ばれる前提)。
    void SetError(const char* fmt, ...);

    DataMapping* GetDataMapping(int file_index);
    bool OpenIndexMapping();
    bool EnsureIndexSize(size_t min_size);

    DISALLOW_COPY_AND_ASSIGN(Rigel);
  };

} // namespace rigel

#endif  // RIGEL_RIGEL_H_
