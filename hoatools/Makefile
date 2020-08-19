# The parser is flex + bison based, everything is generated from
# hoa.l and hoa.y, the tokenizer and parser specifications
hoalexer.c: hoa.l hoaparser.c hoaparser.h simplehoa.c simplehoa.h
	flex --outfile=hoalexer.c --header-file=hoalexer.h hoa.l

hoaparser.c: hoa.y simplehoa.c simplehoa.h
	bison --defines --output=hoaparser.c hoa.y

.PHONY: clean all

clean:
	rm -f hoalexer.h hoalexer.c
	rm -f hoaparser.h hoaparser.c

all: hoalexer.c hoaparser.c
