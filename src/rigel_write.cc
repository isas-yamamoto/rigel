#include <sli/tstring.h>
#include <sli/stdstreamio.h>

#include "rigel.h"

// LAUNCH: 01:31:01, 14 September 2007 UTC
int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
int TI_FINISH = 928693490;

// 2GB
//unsigned long long MAXFILESIZE = 2147483648;

// 128MB
unsigned long long MAXFILESIZE = 134217728;

const int PACKET_SIZE = 1024;

int main(int argc, char** argv) {
  unsigned char buf[PACKET_SIZE];

  sli::stdstreamio sio, ccsds;
  int total_sec;
  int index;
  
  if (ccsds.open("r", argv[1]) < 0) {
    return -1;
  }

  sli::tstring ts = argv[1];
  ts.regreplace(".*/", "");
  ts.regreplace("([0-9]{9})-.*","\\1");
  
  total_sec = ts.atoi();
  index = total_sec - TI_LAUNCH;

  int r;
  r = ccsds.read(buf, PACKET_SIZE);

  rigel::Rigel rigel;
  rigel.Init("/data/rigel/data", "REDACTED.hk");
  rigel.Write(index, buf, r);
  
  return 0;
}
