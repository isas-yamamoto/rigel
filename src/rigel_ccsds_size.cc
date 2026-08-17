/**
 * RIGEL( Reduced and Indexed Giga-data Engine Library )
 *
 */

#include <cstdio>
#include <cstdlib>

// LAUNCH: 01:31:01, 14 September 2007 UTC
const int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
const int TI_FINISH = 928693490;

const int SIZE_CCSDS_HEADER = 6;

#include "rigel.h"

const int PACKET_SIZE = 1024;

int main(int, char** argv) {
  unsigned char buf[PACKET_SIZE];

  int total_sec = std::atoi(argv[1]);
  int index = total_sec - TI_LAUNCH;

  rigel::Rigel rigel;
  if (!rigel.Init("/data/rigel/data")) {
    std::fprintf(stderr, "%s: %s\n", argv[0], rigel.LastError());
    return -1;
  }
  int r = rigel.Read(index,buf,PACKET_SIZE);

  if (r>0) {
    unsigned int len = buf[4];
    len = (len<<8) + buf[5];
    len += SIZE_CCSDS_HEADER + 1;
    std::printf("%u\n", len);
  } else {
    std::printf("none\n");
  }
  return 0;
}
