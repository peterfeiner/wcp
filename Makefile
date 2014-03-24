#CFLAGS = -Wall -Werror
#LDFLAGS = -lrt -lpthread

build:
	python setup.py build
.PHONY: build

a.out: test.o
	$(CC) -o $@ $< $(LDFLAGS)

clean:
	rm -f test.o a.out
	rm -rf build
