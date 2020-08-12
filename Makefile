CXX = clang++
SRCS = hoatools/simplehoa.cpp hoatools/hoaparser.cpp hoatools/hoalexer.cpp
HDRS = hoatools/simplehoa.hpp hoatools/hoaparser.hpp hoatools/hoalexer.hpp

GITVER = $(shell git describe --tags)
CFLAGS = -O3 -DNDEBUG -DGITVER=\"$(GITVER)\"
DBGFLAGS = -fsanitize=address -fno-omit-frame-pointer -g

# The generated files for the parser have a sub Makefile
GEND = hoatools/hoalexer.hpp hoatools/hoalexer.cpp\
       hoatools/hoaparser.hpp hoatools/hoaparser.cpp

$(GEND): hoatools/hoa.l hoatools/hoa.y
	cd hoatools && $(MAKE) all

.PHONY: all clean

acacia: $(SRCS) $(HDRS)
	$(CXX) $(CFLAGS) -o acacia $(SRCS) acacia.cpp

acacia-dbg: $(SRCS) $(HDRS)
	$(CXX) $(DBGFLAGS) -o acacia-dbg $(SRCS) acacia.cpp 

all: acacia acacia-dbg

clean:
	rm acacia acacia-dbg
