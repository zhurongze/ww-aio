test: test.c ww_aio.c ww_aio.h
	gcc -o test test.c ww_aio.c -lzmq -I.
