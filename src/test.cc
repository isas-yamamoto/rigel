/**
 * Rigel クラスの簡易動作確認テスト
 *
 * data/index ファイルを実際に作成し、Write/Read の一致、
 * 複数ファイルへの分割（ロールオーバー）、Scan の列挙、
 * 不正な index に対する安全な失敗を確認する。
 */
#include <sys/stat.h>
#include <sys/types.h>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include "rigel.h"

static int g_fail = 0;

static void check(bool cond, const char* label) {
  if (cond) {
    std::printf("PASS: %s\n", label);
  } else {
    std::printf("FAIL: %s\n", label);
    g_fail++;
  }
}

int main() {
  const char* dir = "/tmp/rigel_test";
  ::mkdir(dir, 0755);

  // block_size=64, max_file_count=2 -> max_file_size=128 -> 1ファイルにつき2ブロック
  const int block_size = 64;
  const int max_file_count = 2;

  rigel::Rigel rigel;
  rigel.Init(dir, "t", block_size, max_file_count);

  unsigned char wbuf[4][block_size];
  for (int i = 0; i < 4; ++i) {
    std::memset(wbuf[i], 'A' + i, block_size);
  }

  // index 0,1 は1ファイル目、2,3 は2ファイル目に分かれる
  for (int i = 0; i < 4; ++i) {
    ssize_t r = rigel.Write(i, wbuf[i], block_size);
    check(r == (ssize_t)block_size, "Write returns full block size");
  }

  for (int i = 0; i < 4; ++i) {
    unsigned char rbuf[block_size];
    ssize_t r = rigel.Read(i, rbuf, block_size);
    check(r == (ssize_t)block_size, "Read returns full block size");
    check(std::memcmp(rbuf, wbuf[i], block_size) == 0, "Read data matches Write data");
  }

  // 一度も書いていない index は index ファイル上のフラグが立たず読めない
  {
    unsigned char rbuf[block_size];
    ssize_t r = rigel.Read(100, rbuf, block_size);
    check(r == -1, "Read of unwritten index fails");
  }

  // 明らかに範囲外の index は file_index が MAX_FILE_INDEX を超え、安全に失敗する
  {
    unsigned char rbuf[block_size];
    long long huge_index = (long long)rigel::MAX_FILE_INDEX * max_file_count + 1000;
    ssize_t r = rigel.Read((int)huge_index, rbuf, block_size);
    check(r == -1, "Read of out-of-range index fails safely");
  }

  // Scan で書き込んだ index 0..3 が列挙されること
  {
    bool seen[4] = {false, false, false, false};
    int count = 0;
    check(rigel.ScanInit(), "ScanInit succeeds");
    int idx;
    while ((idx = rigel.ScanNext()) >= 0) {
      if (idx >= 0 && idx < 4) {
        seen[idx] = true;
      }
      count++;
      if (count > 1000) {
        break; // 無限ループ防止
      }
    }
    check(seen[0] && seen[1] && seen[2] && seen[3], "Scan enumerates all written indices");
  }

  // メタデータ経由のInit(dirname)
  {
    const char* meta_dir = "/tmp/rigel_test_meta";
    ::mkdir(meta_dir, 0755);
    check(rigel::Rigel::WriteMeta(meta_dir, "metatest", 64, 2), "WriteMeta succeeds");

    rigel::Rigel r2;
    check(r2.Init(meta_dir), "Init(dirname) reads metadata successfully");

    unsigned char wbuf2[64], rbuf2[64];
    std::memset(wbuf2, 'Z', 64);
    check(r2.Write(0, wbuf2, 64) == 64, "Write via metadata-initialized Rigel succeeds");
    check(r2.Read(0, rbuf2, 64) == 64 && std::memcmp(wbuf2, rbuf2, 64) == 0,
          "Read via metadata-initialized Rigel matches");

    rigel::Rigel r3;
    check(!r3.Init("/tmp/rigel_test_meta_nonexistent"),
          "Init(dirname) fails when metadata is missing");
  }

  // LastError(): 正常系(未書き込みindexの読み込み)ではセットされず、
  // 誤用(範囲外index)では具体的な理由が載ることを確認する。
  {
    const char* err_dir = "/tmp/rigel_test_lasterror";
    ::mkdir(err_dir, 0755);

    rigel::Rigel r4;
    r4.Init(err_dir, "err", 64, 2);

    unsigned char rbuf[64];
    check(r4.Read(0, rbuf, 64) == -1, "Read of unwritten index still fails");
    check(std::strlen(r4.LastError()) == 0,
          "LastError is empty after a normal not-found Read (not a real error)");

    long long huge_index = (long long)rigel::MAX_FILE_INDEX * 2 + 1000;
    unsigned char wbuf3[64];
    std::memset(wbuf3, 0, 64);
    ssize_t r = r4.Write((int)huge_index, wbuf3, 64);
    check(r == -1, "Write of out-of-range index fails");
    check(std::strstr(r4.LastError(), "out of range") != NULL,
          "LastError reports the out-of-range reason after misuse");
  }

  // スレッド安全性: 複数スレッドから同一Rigelインスタンスへ同時にWrite/Readしても
  // 壊れないことを確認する(shardを跨ぐ書き込みでdata_maps_への挿入も競合させる)。
  {
    const char* mt_dir = "/tmp/rigel_test_mt";
    ::mkdir(mt_dir, 0755);

    const int mt_block_size = 64;
    const int mt_max_file_count = 2; // 2indexごとにshardが変わるようにする
    const int num_threads = 8;
    const int per_thread = 50;

    rigel::Rigel mt_rigel;
    mt_rigel.Init(mt_dir, "mt", mt_block_size, mt_max_file_count);

    // vector<bool>はビット詰め実装のため異なるindexへの同時書き込みでも
    // 同じwordを共有し偽の競合になる(TSanで確認済み)。charを使う。
    std::vector<char> write_ok(num_threads * per_thread, 0);
    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t) {
      threads.push_back(std::thread([&, t]() {
        unsigned char buf[mt_block_size];
        for (int i = 0; i < per_thread; ++i) {
          int idx = t * per_thread + i;
          std::memset(buf, (unsigned char)(t * 16 + i % 16), mt_block_size);
          ssize_t r = mt_rigel.Write(idx, buf, mt_block_size);
          write_ok[idx] = (r == (ssize_t)mt_block_size);
        }
      }));
    }
    for (size_t i = 0; i < threads.size(); ++i) {
      threads[i].join();
    }

    bool all_write_ok = true;
    for (size_t i = 0; i < write_ok.size(); ++i) {
      if (!write_ok[i]) {
        all_write_ok = false;
      }
    }
    check(all_write_ok, "Concurrent Write from multiple threads all succeed");

    bool all_read_ok = true;
    for (int t = 0; t < num_threads; ++t) {
      for (int i = 0; i < per_thread; ++i) {
        int idx = t * per_thread + i;
        unsigned char rbuf[mt_block_size];
        ssize_t r = mt_rigel.Read(idx, rbuf, mt_block_size);
        unsigned char expected = (unsigned char)(t * 16 + i % 16);
        if (r != (ssize_t)mt_block_size) {
          all_read_ok = false;
          continue;
        }
        for (int b = 0; b < mt_block_size; ++b) {
          if (rbuf[b] != expected) {
            all_read_ok = false;
            break;
          }
        }
      }
    }
    check(all_read_ok, "Data written concurrently reads back correctly (no cross-thread corruption)");
  }

  if (g_fail == 0) {
    std::printf("All tests passed\n");
  } else {
    std::printf("%d test(s) failed\n", g_fail);
  }
  return g_fail == 0 ? 0 : 1;
}
