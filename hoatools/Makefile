# The parser is flex + bison based, everything is generated from
# hoa.l and hoa.y, the tokenizer and parser specifications
hoalexer.cpp: hoa.l hoaparser.cpp hoaparser.hpp
	flex --outfile=hoalexer.cpp --header-file=hoalexer.hpp hoa.l

hoaparser.cpp: hoa.y
	bison --defines --output=hoaparser.cpp hoa.y

.PHONY: clean all

clean:
	rm -f hoalexer.hpp hoalexer.cpp
	rm -f hoaparser.hpp hoaparser.cpp

all: hoalexer.cpp hoaparser.cpp
