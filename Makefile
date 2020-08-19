# C stuff for the hoa parser
CC = clang
CFLAGS = -Wall -O3 -DNDEBUG
CSRCS = hoatools/simplehoa.c hoatools/hoaparser.c hoatools/hoalexer.c
CHDRS = hoatools/simplehoa.h hoatools/hoaparser.h hoatools/hoalexer.h

# C++ stuff
CXX = clang++
GITVER = $(shell git describe --tags)
CXXFLAGS = -Wall -O3 -DNDEBUG -DGITVER=\"$(GITVER)\"
DBGFLAGS = -Wall -fsanitize=address -fno-omit-frame-pointer -g
OBJS = hoatools/simplehoa.o hoatools/hoaparser.o hoatools/hoalexer.o
SRCS = 
HDRS = antichain.hpp

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

acacia: $(SRCS) $(HDRS) $(OBJS) $(CHDRS)
	$(CXX) $(CXXFLAGS) -o acacia $(SRCS) $(OBJS) acacia.cpp

acacia-dbg: $(SRCS) $(HDRS) $(OBJS) $(CHDRS)
	$(CXX) $(DBGFLAGS) -o acacia-dbg $(SRCS) $(OBJS) acacia.cpp 

.PHONY: all clean

all: acacia acacia-dbg

clean:
	rm $(OBJS)
	rm acacia acacia-dbg
