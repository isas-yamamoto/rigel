#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

// LAUNCH: 01:31:01, 14 September 2007 UTC
int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
int TI_FINISH = 928693490;

// 2GB
//unsigned long long MAXFILESIZE = 2147483648;

// 128MB
unsigned long long MAXFILESIZE = 134217728;

const int PACKET_SIZE = 1024;

namespace {

// ファイル名（例: ".../123456789-foo.dat"）から先頭9桁のタイムスタンプを取り出す。
int ParseTotalSecFromFilename(const char* path) {
  std::string s(path);
  size_t slash = s.find_last_of('/');
  if (slash != std::string::npos) {
    s = s.substr(slash + 1);
  }
  static const std::regex re("^([0-9]{9})-.*");
  std::smatch m;
  if (std::regex_match(s, m, re)) {
    s = m[1].str();
  }
  return std::atoi(s.c_str());
}

// 既存ファイルなら開き、無ければ新規作成してランダムアクセス用に開く。
bool OpenRandomAccess(std::fstream& fs, const char* filename) {
  fs.open(filename, std::ios::in | std::ios::out | std::ios::binary);
  if (!fs.is_open()) {
    fs.clear();
    fs.open(filename, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
  }
  return fs.is_open();
}

} // namespace

int main(int argc, char** argv) {
  char buf[PACKET_SIZE];

  std::ifstream ccsds(argv[1], std::ios::binary);
  if (!ccsds.is_open()) {
    return -1;
  }

  std::fstream fi;
  OpenRandomAccess(fi, "data/REDACTED.hk.index");

  int num_file = (int)((double)(TI_FINISH - TI_LAUNCH) * PACKET_SIZE / MAXFILESIZE) + 1;

  std::vector<std::fstream> sdata(num_file);
  for(int i=0; i<num_file; ++i) {
    char filename[64];
    std::snprintf(filename, sizeof(filename), "data/REDACTED.hk.%03d", i);
    OpenRandomAccess(sdata[i], filename);
  }

  int total_sec = ParseTotalSecFromFilename(argv[1]);
  int index = total_sec - TI_LAUNCH;

  unsigned long long offset = index;
  offset *= 1024;

  int file_index = offset / MAXFILESIZE;
  int file_offset = offset % MAXFILESIZE;

  std::printf("%d %d %llu %d\n", total_sec, index, offset, file_index);

  if ( file_index < 0 || file_index >= num_file ) {
    std::printf("invalid file index: %d\n", num_file);
    return -1;
  }

  std::streamsize r;
  while ( ccsds.read(buf, PACKET_SIZE), (r = ccsds.gcount()) > 0 ) {
    sdata[file_index].seekp(file_offset, std::ios::beg);
    sdata[file_index].write(buf, r);
  }

  buf[0] = 1;
  fi.seekp(index, std::ios::beg);
  fi.write(buf, 1);

  return 0;
}
