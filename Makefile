test: test.c ww_aio.c ww_aio.h
	gcc -o test test.c ww_aio.c -lzmq -I.

test2: test2.c ww_aio.c ww_aio.h
	gcc -o test2 test2.c ww_aio.c -lzmq -I.

volume: volume.c ww_aio.c ww_aio.h
	gcc -o volume volume.c ww_aio.c -lzmq -I.

lib: ww_aio.c ww_aio.h
	gcc -fPIC -c ww_aio.c -lzmq -I.
	ar -r libww_aio.a ww_aio.o
	ar -s libww_aio.a ww_aio.o
	ranlib libww_aio.a

install: lib
	cp ww_aio.h /usr/local/include/
	cp libww_aio.a /usr/local/lib

a: a.c
	gcc -o a a.c -lzmq -I.

b: b.c
	gcc -o b b.c -lzmq -I.

zrz: a b
	echo "ok"

all: test test2 volume lib install
