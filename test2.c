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
    int data_len = 4096;

    img_aio_ctx_t *ctx;
    img_aio_comp_t *comp;
    ctx = img_aio_setup();
    s_sleep(1000);

    printf("the game is running\n");
    int fd = 0;
    int rc = 0;
    int size = 1024000000;

    rc = img_create(ctx, "test3.img", size, 22);
    assert(rc == 0);
    fd = img_open(ctx, "test3.img");
    assert(fd > 0);

    img_info_t img;
    rc = img_stat(ctx, fd, &img);
    assert(rc == 0);

    rc = img_stat(ctx, fd, &img);
    assert(rc == 0);

    rc = img_stat(ctx, fd, &img);
    assert(rc == 0);


    int i = 0;
    int ret = 0;
    img_aio_comp_t *comps[100];
    memset((void *)comps, 0, sizeof(comps));
    char cc = 'a';

    for(i = 0; i < 100; i++)
    {
        printf("exec %d async write op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        printf("====== comp = %lu\n", (uint64_t)comp);
        comps[i] = comp;
        memset(buf, cc, data_len);
        cc++;
        img_aio_write(comp, fd, buf, data_len, i * data_len);
    }

    for(i = 0; i < 100; i++)
    {
        comp = comps[i];
        img_aio_wait_for_ack(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d write op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
    }

    img_read(ctx, fd, buf, 4096, size - 4096);

    img_close(ctx, fd);
    assert(rc == 0);

    s_sleep(1000);
    return 0;
}
