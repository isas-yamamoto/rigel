/**
 * RIGEL( Reduced and Indexed Giga-data Engine Library )
 *
 */

#include <sli/tstring.h>
#include <sli/stdstreamio.h>

// LAUNCH: 01:31:01, 14 September 2007 UTC
const int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
const int TI_FINISH = 928693490;

const int SIZE_CCSDS_HEADER = 6;

#include "rigel.h"

// 
const int PACKET_SIZE = 1024;

int main(int argc, char** argv) {
  unsigned char buf[PACKET_SIZE];

  rigel::Rigel rigel;
  rigel.Init("/data/rigel/data", "REDACTED.hk");

  sli::tstring ti = argv[1];
  int total_sec = ti.atoi();
  int index = total_sec - TI_LAUNCH;

  sli::stdstreamio sio;
  rigel.ScanInit();
  long long val;
  for(; (val=rigel.ScanNext()) >= 0; index++) {
    int r = rigel.Read(index,buf,PACKET_SIZE);
    if (r>0) {
      unsigned int len = buf[4];
      len = (len<<8) + buf[5];
      len += SIZE_CCSDS_HEADER + 1;
      sio.printf("%d\n", len);
    } else {
      sio.printf("none\n");
    }
  }
  return 0;
}
