CXX=g++
CXXFLAGS=-Wall -O3 -std=c++11 -fPIC -pthread
LDFLAGS=-pthread
OBJS=rigel.o

# Keep in sync with rigel::VERSION in rigel.h.
VERSION=1.0.0

PREFIX ?= /usr/local
INCLUDEDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin

.SUFFIXES: .cc .o .so

.PHONY: all
all: rigel librigel.so librigel.a test

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

rigel: rigel_cli.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

test: test.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

test.o: ../tests/test.cc
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

librigel.so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS)


librigel.a: $(OBJS)
	ar rv $@ $(OBJS)


.PHONY: check
check: test
	LD_LIBRARY_PATH=. ./test

.PHONY: install
install: all
	install -d $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(BINDIR)
	install -m 644 rigel.h $(DESTDIR)$(INCLUDEDIR)/
	install -m 644 librigel.so $(DESTDIR)$(LIBDIR)/
	install -m 644 librigel.a $(DESTDIR)$(LIBDIR)/
	install -m 755 rigel $(DESTDIR)$(BINDIR)/

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/rigel.h
	$(RM) $(DESTDIR)$(LIBDIR)/librigel.so $(DESTDIR)$(LIBDIR)/librigel.a
	$(RM) $(DESTDIR)$(BINDIR)/rigel

.PHONY: clean
clean:
	$(RM) rigel.o rigel_cli.o rigel librigel.so librigel.a test.o test
