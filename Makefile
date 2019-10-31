SRCS = src/veclist.c src/vecutil.c src/kdtree.c
HDRS = src/veclist.h src/vecutil.h src/kdtree.h

CFLAGS = -g  # O3

.PHONY: tests clean
tests: src/tests.c $(SRCS) $(HDRS)
	gcc $(CFLAGS) src/tests.c $(SRCS) -o tests
	./tests

clean:
	rm tests
