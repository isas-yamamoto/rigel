# For RIGEL (Next SIRIUS Project)

CXX=g++
CXXFLAGS=-Wall -O3 -std=c++11 -fPIC -pthread
LDFLAGS=-pthread
OBJS=rigel.o

.SUFFIXES: .cc .o .so

.PHONY: all
all: rigel librigel.so librigel.a test

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

rigel: rigel_cli.o librigel.so
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
	$(RM) rigel.o rigel_cli.o rigel librigel.so librigel.a test.o test
