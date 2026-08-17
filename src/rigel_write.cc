/**
 * RIGEL( Reduced and Indexed Giga-data Engine Library )
 *
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

#include "rigel.h"

// LAUNCH: 01:31:01, 14 September 2007 UTC
const int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
const int TI_FINISH = 928693490;

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

} // namespace

int main(int argc, char** argv) {
  unsigned char buf[PACKET_SIZE];

  std::ifstream ccsds(argv[1], std::ios::binary);
  if (!ccsds.is_open()) {
    return -1;
  }

  int total_sec = ParseTotalSecFromFilename(argv[1]);
  int index = total_sec - TI_LAUNCH;

  ccsds.read(reinterpret_cast<char*>(buf), PACKET_SIZE);
  std::streamsize r = ccsds.gcount();

  rigel::Rigel rigel;
  if (!rigel.Init("/data/rigel/data")) {
    std::fprintf(stderr, "%s: %s\n", argv[0], rigel.LastError());
    return -1;
  }
  if (rigel.Write(index, buf, r) < 0) {
    std::fprintf(stderr, "%s: %s\n", argv[0], rigel.LastError());
    return -1;
  }

  return 0;
}
