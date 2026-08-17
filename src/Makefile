CXX=g++
CC=gcc
CXXFLAGS=-Wall -O3 -std=c++11 -fPIC -pthread
CFLAGS=-Wall -O3 -std=c99
LDFLAGS=-pthread
OBJS=rigel.o rigel_meta.o rigel_c.o

# Keep in sync with rigel::VERSION in rigel.h.
VERSION=1.0.0

PREFIX ?= /usr/local
INCLUDEDIR=$(PREFIX)/include
LIBDIR=$(PREFIX)/lib
BINDIR=$(PREFIX)/bin

.SUFFIXES: .cc .o .so

.PHONY: all
all: rigel librigel.so librigel.a test test_c test_cli

.cc.o:
	$(CXX) $(CXXFLAGS) -c $<

rigel: rigel_cli.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

test: test.o librigel.so
	$(CXX) $(LDFLAGS) -o $@ $< -L. -lrigel

test.o: ../tests/test.cc
	$(CXX) $(CXXFLAGS) -I. -c $< -o $@

# Plain C, to prove rigel_c.h is actually callable from C.
test_c: test_c.o librigel.so
	$(CC) $(LDFLAGS) -o $@ $< -L. -lrigel

test_c.o: ../tests/test_c.c
	$(CC) $(CFLAGS) -I. -c $< -o $@

# Drives the built ./rigel binary as a subprocess, so it needs rigel built
# first; RIGEL_CLI_PATH is this directory's ./rigel (an absolute path, so
# the test doesn't depend on the working directory it's run from).
test_cli: test_cli.o rigel
	$(CXX) $(LDFLAGS) -o $@ $<

test_cli.o: ../tests/test_cli.cc rigel
	$(CXX) $(CXXFLAGS) -I. -DRIGEL_CLI_PATH='"$(CURDIR)/rigel"' -c $< -o $@

librigel.so: $(OBJS)
	$(CXX) $(CXXFLAGS) -shared -o $@ $(OBJS)


librigel.a: $(OBJS)
	ar rv $@ $(OBJS)


.PHONY: check
check: test test_c test_cli
	LD_LIBRARY_PATH=. ./test
	LD_LIBRARY_PATH=. ./test_c
	LD_LIBRARY_PATH=. ./test_cli

.PHONY: install
install: all
	install -d $(DESTDIR)$(INCLUDEDIR) $(DESTDIR)$(LIBDIR) $(DESTDIR)$(BINDIR)
	install -m 644 rigel.h rigel_c.h $(DESTDIR)$(INCLUDEDIR)/
	install -m 644 librigel.so $(DESTDIR)$(LIBDIR)/
	install -m 644 librigel.a $(DESTDIR)$(LIBDIR)/
	install -m 755 rigel $(DESTDIR)$(BINDIR)/

.PHONY: uninstall
uninstall:
	$(RM) $(DESTDIR)$(INCLUDEDIR)/rigel.h $(DESTDIR)$(INCLUDEDIR)/rigel_c.h
	$(RM) $(DESTDIR)$(LIBDIR)/librigel.so $(DESTDIR)$(LIBDIR)/librigel.a
	$(RM) $(DESTDIR)$(BINDIR)/rigel

.PHONY: clean
clean:
	$(RM) rigel.o rigel_meta.o rigel_c.o rigel_cli.o rigel librigel.so librigel.a test.o test test_c.o test_c test_cli.o test_cli
