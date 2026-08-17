# For RIGEL (Next SIRIUS Project)

CXX=s++
CXXFLAGS=-Wall -O3 -I/usr/local/include
OBJS=rigel.o

.SUFFIXES: .cc .o .so

.PHONY: all
all: rigel_read rigel_write librigel.so librigel.a rigel_ccsds_size rigel_scan

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

rigel_read: rigel_read.o librigel.so
	$(CXX) -o $@ $< -L. -lrigel

rigel_write: rigel_write.o librigel.so
	$(CXX) -o $@ $< -L. -lrigel

rigel_ccsds_size: rigel_ccsds_size.o librigel.so
	$(CXX) -o $@ $< -L. -lrigel

rigel_scan: rigel_scan.o librigel.so
	$(CXX) -o $@ $< -L. -lrigel

librigel.so: $(OBJS)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $(OBJS)


librigel.a: $(OBJS)
	ar rv $@ $(OBJS)


.PHONY: clean
clean:
	$(RM) rigel_read.o rigel_write.o rigel_ccsds_size.o rigel_read rigel_write rigel_ccsds_size librigel.so rigel_scan.o rigel_scan
