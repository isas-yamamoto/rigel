/**
 * Rigel Read/Write のベンチマーク（毎回open/closeする現行実装のベースライン計測）
 */
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "rigel.h"

int main(int argc, char** argv) {
  const char* dir = "/tmp/rigel_bench";
  ::mkdir(dir, 0755);

  const int block_size = 1024;
  const int max_file_count = 131072; // default MAX_FILE_COUNT
  const int n = (argc > 1) ? std::atoi(argv[1]) : 100000;

  rigel::Rigel rigel;
  rigel.Init(dir, "bench", block_size, max_file_count);

  unsigned char buf[block_size];
  std::memset(buf, 0xAB, block_size);
  unsigned char rbuf[block_size];

  // Write ベンチ（新規index、逐次）
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < n; ++i) {
    rigel.Write(i, buf, block_size);
  }
  auto t1 = std::chrono::steady_clock::now();

  // Read ベンチ（既存index、逐次）
  for (int i = 0; i < n; ++i) {
    rigel.Read(i, rbuf, block_size);
  }
  auto t2 = std::chrono::steady_clock::now();

  double write_ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / n;
  double read_ns  = std::chrono::duration<double, std::nano>(t2 - t1).count() / n;

  std::printf("n=%d block_size=%d\n", n, block_size);
  std::printf("Write: %.1f ns/op (%.0f ops/sec)\n", write_ns, 1e9 / write_ns);
  std::printf("Read:  %.1f ns/op (%.0f ops/sec)\n", read_ns, 1e9 / read_ns);

  return 0;
}
