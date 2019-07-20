tests: tests.c rbtree.h rbtree.c
	gcc -O3 tests.c rbtree.c -o tests
