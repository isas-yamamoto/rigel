# For RIGEL (Next SIRIUS Project)

CXX=s++
CXXFLAGS=-Wall -O3 -std=c++11 -fPIC -pthread
LDFLAGS=-pthread
OBJS=rigel.o

.SUFFIXES: .cc .o .so

.PHONY: all
all: rigel_read rigel_write librigel.so librigel.a rigel_ccsds_size rigel_scan rigel_init test

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

rigel_read: rigel_read.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

rigel_write: rigel_write.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

rigel_ccsds_size: rigel_ccsds_size.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

rigel_scan: rigel_scan.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

rigel_init: rigel_init.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

test: test.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

librigel.so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS)


librigel.a: $(OBJS)
	ar rv $@ $(OBJS)


.PHONY: check
check: test
	LD_LIBRARY_PATH=. ./test

.PHONY: clean
clean:
	$(RM) rigel.o rigel_read.o rigel_write.o rigel_ccsds_size.o rigel_read rigel_write rigel_ccsds_size librigel.so librigel.a rigel_scan.o rigel_scan rigel_init.o rigel_init test.o test
