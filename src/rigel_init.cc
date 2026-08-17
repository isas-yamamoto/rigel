#include <sli/tstring.h>
#include <sli/stdstreamio.h>

// LAUNCH: 01:31:01, 14 September 2007 UTC
int TI_LAUNCH = 873768659;

// FINISH: 18:25:00, 10 Jun 2009 UTC
int TI_FINISH = 928693490;

// 2GB
//unsigned long long MAXFILESIZE = 2147483648;

// 128MB
unsigned long long MAXFILESIZE = 134217728;

const int PACKET_SIZE = 1024;

#define UTCLEN 30
#define MAX_SCLK 1024

double char2double(const char* s);

int main(int argc, char** argv) {
  char buf[PACKET_SIZE];

  sli::stdstreamio sio, ccsds;
  int total_sec;
  int index;
  int file_index, file_offset;
  unsigned long long offset;

  if (ccsds.open("r", argv[1]) < 0) {
    return -1;
  }

  sli::stdstreamio fi;
  fi.open("a+","data/REDACTED.hk.index");

  int num_file = (int)((double)(TI_FINISH - TI_LAUNCH) * PACKET_SIZE / MAXFILESIZE) + 1;

  sli::stdstreamio sdata[num_file];
  for(int i=0; i<num_file; ++i) {
    sli::tstring filename;
    filename.printf("data/REDACTED.hk.%03d", i);
    sdata[i].open("a+", filename.cstr());
  }


  sli::tstring ts = argv[1];
  ts.regreplace(".*/", "");
  ts.regreplace("([0-9]{9})-.*","\\1");
  
  total_sec = ts.atoi();
  index = total_sec - TI_LAUNCH;

  offset = index;
  offset *= 1024;

  file_index = offset / MAXFILESIZE;
  file_offset = offset % MAXFILESIZE;

  sio.printf("%d %d %ld %d\n", total_sec, index, offset, file_index);

  if ( file_index < 0 || file_index >= num_file ) {
    sio.printf("invalid file index: %d\n", num_file);
    return -1;
  }

  int r;
  while ( (r = ccsds.read(buf, PACKET_SIZE)) > 0 ) {
    sdata[file_index].seek(file_offset, SEEK_SET);
    sdata[file_index].write(buf, r);
  }

  buf[0] = 1;
  fi.seek(index, SEEK_SET);
  fi.write(buf,1);
  
  return 0;
}

double char2double(const char* s)
{
  double ret;
  char *p = (char*)s;
  
  ret = 0.0;
  while(*p) {
    ret = (ret*10) + (*p - '0');
    p++;
  }
  
  return ret;
}
