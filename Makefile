CXX = clang++
SRCS = hoatools/simplehoa.cpp hoatools/hoaparser.cpp hoatools/hoalexer.cpp
HDRS = hoatools/simplehoa.hpp hoatools/hoaparser.hpp hoatools/hoalexer.hpp

CFLAGS = -O3 -DNDEBUG
DBGFLAGS = -fsanitize=address -fno-omit-frame-pointer -g

# The generated files for the parser have a sub Makefile
GEND = hoatools/hoalexer.hpp hoatools/hoalexer.cpp\
       hoatools/hoaparser.hpp hoatools/hoaparser.cpp

$(GEND):
	cd hoatools && $(MAKE)

.PHONY: all clean

acacia: $(SRCS) $(HDRS)
	$(CXX) $(CFLAGS) -o acacia $(SRCS) acacia.c 

acacia-dbg: $(SRCS) $(HDRS)
	$(CXX) $(DBGFLAGS) -o acacia $(SRCS) acacia.c 

all: acacia acacia-dbg

clean:
	rm tests
