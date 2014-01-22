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
    int ret = 0;
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
    img_op_t *op;
    zmq_msg_t msg;
    int len;
    while(1)
    {
        rc = zmq_msg_init(&msg);
        assert(rc == 0);
        rc = zmq_msg_recv(&msg, vol_pull_sock, 0);
        assert(rc != -1);

        op = (img_op_t *)zmq_msg_data(&msg);
        op->ret = ret++;
        len = op->len + sizeof(img_op_t);
        printf("volume recv tid_t=%ld op\n", op->seq);
        rc = zmq_msg_send(&msg, vol_push_sock, 0);
        assert(rc == len);
    }
}

void aio_callback_func(img_aio_comp_t *comp, void *arg)
{
    printf("run callback func for tid_t=%ld op\n", comp->op.seq);
}

//void send_msg_test()
//{
//    //send
//    zmq_msg_t msg;
//    int rc = zmq_msg_init_size(&msg, 1024);
//    assert(rc == 0);
//    
//    memset(zmq_msg_data(&msg), 'A', 1024);
//
//    rc = zmq_msg_send(&msg, socket, 0);
//
//    assert(rc == 6);
//}
//
//void recv_msg_test()
//{
//    //recv
//    zmq_msg_t msg;
//    int rc = zmq_msg_init(&msg);
//    assert(rc == 0);
//
//    //block until a message is available to be received from socket
//    rc = zmq_msg_recv(&msg, socket, 0);
//    assert(rc != -1);
//
//}


int main()
{
    pthread_t volume_td;
    void *zm_ctx = zmq_ctx_new();
    char buf[4096];

    s_sleep(100);
    pthread_create(&volume_td, NULL, volume_func, zm_ctx);
    s_sleep(100);

    img_aio_ctx_t *ctx;
    img_aio_comp_t *comp;
    ctx = img_aio_setup();
    s_sleep(1000);

    printf("the game is running\n");

    int i = 0;
    int ret = 0;
    for(i = 0; i < 2000; i++)
    {
        printf("exec %d write op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        img_aio_write(comp, 0, buf, 4096, 0);
        img_aio_wait_for_cb(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d write op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
    }

    for (i = 3000; i< 4000; i++) {
        printf("exec %d write op\n", i);
        ret = img_write(ctx, 1, buf, 4096, 0);
        printf("%d write op finished, ret=%d\n", i, ret);
    }

    s_sleep(1000);
    return 0;
}
