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

#include "rigel.h"

const int PACKET_SIZE = 1024;

int main(int argc, char** argv) {
  unsigned char buf[PACKET_SIZE];

  int total_sec = std::atoi(argv[1]);
  int index = total_sec - TI_LAUNCH;

  rigel::Rigel rigel;
  if (!rigel.Init("/data/rigel/data")) {
    return -1;
  }
  int r = rigel.Read(index,buf,PACKET_SIZE);

  if (r>0) {
    for(int i=0; i<PACKET_SIZE; ++i) {
      if (i%16 == 0 ) {
        std::printf("%08X: ", i);
      }
      if (i%16 == 8 ) {
        std::putchar(' ');
      }
      std::printf(" %02x", buf[i] & 0xff);
      if (i%16 == 15) {
        std::putchar('\n');
      }
    }
    std::putchar('\n');
  } else {
    std::printf("none\n");
  }
  return 0;
}
