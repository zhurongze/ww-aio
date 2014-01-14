#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <zmq.h>
#include <assert.h>
#include <stdio.h>


#include "ww_aio.h"

void s_sleep(int ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&t, NULL);
}

void *volume_func(void *arg)
{
    int rc = 0;
    void *ctx = arg;
    void * vol_pull_sock = NULL;
    void * vol_push_sock = NULL;
    vol_pull_sock = zmq_socket(ctx, ZMQ_PULL); 
    vol_push_sock = zmq_socket(ctx, ZMQ_PUSH); 

    rc = zmq_bind(vol_pull_sock, "tcp://*:9001");
    assert(rc == 0);
    rc = zmq_connect(vol_push_sock, "tcp://localhost:9000");
    assert(rc == 0);

    printf("volume thread is running\n");
    op_t op;
    while(1)
    {
        zmq_recv(vol_pull_sock, (void *)&op, sizeof(op_t), 0);
        op.ret = 0;
        printf("volume recv tid_t=%ld op\n", op.seq);
        zmq_send(vol_push_sock, (void *)&op, sizeof(op_t), 0);
    }
}

void aio_callback_func(aio_comp_t *comp, void *arg)
{
    printf("run callback func for tid_t=%ld op\n", comp->op.seq);
}

int main()
{
    pthread_t volume_td;
    void *zm_ctx = zmq_ctx_new();

    s_sleep(100);
    pthread_create(&volume_td, NULL, volume_func, zm_ctx);
    s_sleep(100);

    aio_context_t *ctx;
    aio_comp_t *comp;
    ctx = ww_aio_setup();
    s_sleep(1000);

    printf("the game is running\n");

    int i = 0;
    for(i = 0; i < 2000; i++)
    {
        printf("exec %d write op\n", i);
        comp = ww_aio_create_completion(ctx, aio_callback_func, NULL);
        ww_aio_write(comp, 0, NULL, 4096, 0);
        ww_aio_wait_for_callbacked(comp);
        printf("%d write op finished\n", i);
        //ww_aio_release(comp);
    }

    s_sleep(1000);
    return 0;
}
