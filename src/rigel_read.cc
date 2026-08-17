/**
 * RIGEL( Reduced and Indexed Giga-data Engine Library )
 *
 */

#include <sli/tstring.h>
#include <sli/stdstreamio.h>

// LAUNCH: 01:31:01, 14 September 2007 UTC
int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
int TI_FINISH = 928693490;

#include "rigel.h"

// 
const int PACKET_SIZE = 1024;

int main(int argc, char** argv) {
  unsigned char buf[PACKET_SIZE];

  sli::tstring ti = argv[1];
  int total_sec = ti.atoi();
  int index = total_sec - TI_LAUNCH;

  rigel::Rigel rigel;
  rigel.Init("/data/rigel/data", "REDACTED.hk");
  int r = rigel.Read(index,buf,PACKET_SIZE);

  sli::stdstreamio sio;
  if (r>0) {
    for(int i=0; i<PACKET_SIZE; ++i) {
      if (i%16 == 0 ) {
        sio.printf("%08X: ", i);
      }
      if (i%16 == 8 ) {
        sio.putchr(' ');
      }
      sio.printf(" %02x", buf[i] & 0xff);
      if (i%16 == 15) {
        sio.putchr('\n');
      }
    }
    sio.putchr('\n');
  } else {
    sio.printf("none\n");
  }  
  return 0;
}
