# VelaPack Makefile (GNU make / mingw32-make compatible)
# Targets:
#   all      - Build library and CLI tool
#   test     - Run unit tests
#   clean    - Remove build artifacts
#   install  - Install headers, static library, and CLI (prefix=$(PREFIX))

CXX      = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra -Iinclude
PREFIX   = /usr/local

SRC = src/huffman.cpp src/vz1.cpp src/format.cpp src/libvlp.cpp
OBJ = $(SRC:.cpp=.o)

LIB  = libvlp.a
CLI  = vlp
TEST = test_roundtrip

.PHONY: all test clean install

all: $(LIB) $(CLI)

$(LIB): $(OBJ)
	ar rcs $@ $(OBJ)

$(CLI): $(OBJ) src/cli.cpp
	$(CXX) $(CXXFLAGS) $(OBJ) src/cli.cpp -o $@

$(TEST): $(OBJ) tests/test_roundtrip.cpp
	$(CXX) $(CXXFLAGS) $(OBJ) tests/test_roundtrip.cpp -o $@

test: $(TEST)
	./$(TEST)

install: all
	mkdir -p $(PREFIX)/include $(PREFIX)/lib $(PREFIX)/bin
	cp include/vlp.h $(PREFIX)/include/
	cp $(LIB) $(PREFIX)/lib/
	cp $(CLI) $(PREFIX)/bin/

clean:
	rm -f $(OBJ) $(LIB) $(CLI) $(TEST) *.exe

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
