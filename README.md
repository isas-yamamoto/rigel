# Rigel

**RIGEL** ( Reduced and Indexed Giga-data Engine Library )

連番の整数ID（0, 1, 2, ...）をキーに、固定長レコードを高速に読み書きするための
C++ライブラリ。衛星テレメトリ(REDACTED/REDACTED搭載機器)のような、時刻に対応する
固定サイズパケットを大量に蓄積・参照する用途を想定している。

## 特徴

- **index = 整数ID → offset は計算のみ (`index * block_size`)**。B木もハッシュ
  も持たない。位置計算はO(1)。
- 1本のファイルが肥大化しないよう、`max_file_count` ブロックごとに物理ファイル
  を分割する（shard）。`index` から `file_index`/`file_offset` を割り出して
  該当shardにアクセスする。
- 各indexに「書き込み済みか」を1byteで持つ別ファイル(`.index`)を併設。未書き込み
  のindexを読もうとすると失敗を返す。
- データファイル・indexファイルは `mmap` している。一度開いたら閉じず、以後は
  ポインタ演算 + `memcpy` のみで読み書きする（詳細は下記ベンチマーク参照）。
- 1ディレクトリ = 1系列(key)。`rigel_init` コマンドで `key`/`block_size`/
  `max_file_count` をディレクトリ配下のメタデータファイルに書いておけば、
  以後は `Init(dirname)` だけで済む（下記参照）。

## ビルド

```sh
cd src
make CXX=g++          # ライブラリ・付属ツール・testを一括ビルド
make CXX=g++ check     # test を実行
```

`Makefile` の `CXX` 既定値は社内独自コンパイラ `s++` になっているので、通常の
g++/clang++ 環境では `CXX=g++` を明示する。C++11以上が必要（`std::regex`,
`std::unordered_map`, `std::fstream` の move対応を使用）。

生成物:

| 種類 | 名前 |
|---|---|
| 共有ライブラリ | `librigel.so` |
| 静的ライブラリ | `librigel.a` |
| ヘッダ | `rigel.h` |
| CLIツール | `rigel_read` `rigel_write` `rigel_scan` `rigel_ccsds_size` `rigel_init` |
| テスト | `test` |
| ベンチマーク | `bench` |

## 使い方

まず `rigel_init` でディレクトリを初期化する（1回だけ）。

```sh
./rigel_init /path/to/data mykey 1024 131072   # dirname key block_size max_file_count
```

これで `/path/to/data/rigel.meta` にkey/block_size/max_file_countが書き込まれる。
既にメタデータがあるディレクトリに対しては（既存データが壊れるため）実行を拒否
する。block_size/max_file_countを省略すると既定値（`BLOCK_SIZE`=1024,
`MAX_FILE_COUNT`=131072）が使われる。

あとはコード側は `Init(dirname)` だけで良い:

```cpp
#include "rigel.h"

rigel::Rigel rigel;
if (!rigel.Init("/path/to/data")) {
  // rigel_init前 or メタデータ破損
}

unsigned char buf[1024] = { ... };
rigel.Write(index, buf, sizeof(buf));

unsigned char rbuf[1024];
ssize_t n = rigel.Read(index, rbuf, sizeof(rbuf));  // 未書き込みなら -1

rigel.ScanInit();
int idx;
while ((idx = rigel.ScanNext()) >= 0) {
  // idx に書き込まれたレコードが存在する
}
```

block_size/max_file_countをコード側で明示的に指定したい場合（テストで小さい
shardを使いたい場合など）は、従来通り `Init(dirname, key, block_size,
max_file_count)` も使える。

`dirname` 配下には `rigel.meta` の他、`<key>.<file_index 4桁>` というデータ
ファイルと `<key>.index` というインデックスファイルが作られる。

## テスト・ベンチマーク

- `src/test.cc` : Write/Read一致、複数ファイルへの分割、Scan列挙、範囲外indexの
  安全な失敗を確認する実動作テスト（`make check` で実行）。
- `src/bench.cc` : Read/Writeのスループット計測。`./bench <n> [seq|rand]` で
  件数とアクセス順序を指定できる。

参考値（block_size=1024, n=20000, 開発マシンでの実測）:

| 実装 | Write | Read |
|---|---|---|
| 毎回open/close（旧実装） | 14110 ns/op | 14009 ns/op |
| ファイルハンドル永続化 | 6643 ns/op | 6457 ns/op |
| + 冗長なseekの省略 | 606 ns/op | 321 ns/op |
| mmap化（順次アクセス） | 788 ns/op | 138 ns/op |
| mmap化（ランダムアクセス） | 724 ns/op | 159 ns/op |

mmap化後のReadは初期実装比で100倍以上。Writeの初回アクセスは新規ページに対する
kernelのpage fault分だけ重いが、既存ページへの再書き込みは約80 ns/opまで下がる
（Read同等）。書き込みは基本1回、読み出しは繰り返し行うテレメトリ用途に対して
理にかなったトレードオフ。

## 制約・注意点

- `Read`/`Write`ともfsync/msyncは行わない。OSクラッシュ・電源断に対する耐性は
  ページキャッシュ止まり（プロセスクラッシュには強い）。
- indexファイルは書き込まれた最大indexに応じて伸長する（1MiB単位）。データ
  ファイルは `block_size * max_file_count` で決まる固定長。
