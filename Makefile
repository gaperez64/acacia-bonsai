SRCS = src/veclist.c src/vecutil.c src/kdtree.c
HDRS = src/veclist.h src/vecutil.h src/kdtree.h

CFLAGS = -O3 -DNDEBUG

.PHONY: tests clean all

all: $(SRCS) $(HDRS)
	gcc $(CFLAGS) src/main.c $(SRCS) -o acacia

tests: src/tests.c $(SRCS) $(HDRS)
	gcc -g -fsanitize=address src/tests.c $(SRCS) -o tests
	ASAN_OPTIONS=detect_leaks=1
	./tests

clean:
	rm tests
