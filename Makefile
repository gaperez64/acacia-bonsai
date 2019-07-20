.PHONY: tests
tests: src/tests.c src/rbtree.h src/rbtree.c
	gcc -O3 src/tests.c src/rbtree.c -o tests
	./tests
