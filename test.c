#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <zmq.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>


#include "ww_aio.h"

#define MAX_VOLUMES     31

#define     SUCCESS     0
#define     FAIL        1
#define     NOEXIST     2

#define     BUF_SIZE    1048576

void s_sleep(int ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&t, NULL);
}


void my_free(void *data, void *hint)
{

}

void aio_callback_func(img_aio_comp_t *comp, void *arg)
{
    printf("Callback func\n");

}


int main()
{
    char *buf = NULL;
    buf = malloc(BUF_SIZE);
    assert(buf != NULL);
    int data_len = 12234;

    img_aio_ctx_t *ctx;
    img_aio_comp_t *comp;
    ctx = img_aio_setup();
    s_sleep(1000);

    printf("the game is running\n");
    int fd = 0;
    int rc = 0;

    rc = img_create(ctx, "test3.img", 123456789, 22);
    assert(rc == 0);
    fd = img_open(ctx, "test3.img");
    assert(fd > 0);

    img_info_t img;
    rc = img_stat(ctx, fd, &img);
    assert(rc == 0);
    printf("\n\n");
    printf("img name=%s\n", img.name);
    printf("img size=%ld\n", img.size);


    int i = 0;
    int ret = 0;
    char cc = 'a';
    for(i = 0; i < 100; i++)
    {
        printf("exec %d async write op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        memset(buf, cc, data_len);
        cc++;
        img_aio_write(comp, fd, buf, data_len, i * data_len);
        img_aio_wait_for_ack(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d write op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
    }

    for (; i< 200; i++) {
        printf("exec %d sync write op\n", i);
        memset(buf, cc, data_len);
        printf("cc=%d,buf[0]=%d\n", (int)cc, (int)(buf[0]));
        cc++;
        ret = img_write(ctx, fd, buf, data_len, i * data_len);
        printf("%d write op finished, ret=%d\n", i, ret);
    }

    cc = 'a';
    for(i = 0; i < 100; i++)
    {
        printf("exec %d async read op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        memset(buf, 0, data_len);
        img_aio_read(comp, fd, buf, data_len, i * data_len);
        img_aio_wait_for_ack(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d read op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
        assert(cc == buf[0]);
        cc++;
    }

    for (; i< 200; i++) {
        printf("exec %d sync read op\n", i);
        ret = img_read(ctx, fd, buf, data_len, i * data_len);
        printf("%d read op finished, ret=%d\n", i, ret);
        assert(cc == buf[0]);
        cc++;
    }


    img_close(ctx, fd);
    assert(rc == 0);

    s_sleep(1000);
    return 0;
}
