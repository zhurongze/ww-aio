#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <zmq.h>
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>


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
    char buf[8192];
    void *data = NULL;
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

        data = zmq_msg_data(&msg);
        op = (img_op_t *)data;
        op->ret = ret++;
        len = op->len + sizeof(img_op_t);
        printf("volume recv tid_t=%ld op\n", op->seq);
        rc = zmq_msg_send(&msg, vol_push_sock, 0);
        assert(rc == len);
    }
}

void *file_volume_func(void *arg)
{
    int rc = 0;
    int ret = 0;
    void *ctx = arg;
    void * vol_pull_sock = NULL;
    void * vol_push_sock = NULL;
    char buf[8192];
    vol_pull_sock = zmq_socket(ctx, ZMQ_PULL); 
    vol_push_sock = zmq_socket(ctx, ZMQ_PUSH); 

    rc = zmq_bind(vol_pull_sock, "tcp://*:9001");
    assert(rc == 0);
    rc = zmq_connect(vol_push_sock, "tcp://localhost:9000");
    assert(rc == 0);

    int fd;
    fd = open("test.img", O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    assert(fd > 0);

    printf("file volume thread is running\n");
    img_op_t *op;
    zmq_msg_t recv_msg, send_msg;
    void *recv_data = NULL;
    void *send_data = NULL;
    int len;

    while(1)
    {
        rc = zmq_msg_init(&recv_msg);
        assert(rc == 0);
        rc = zmq_msg_recv(&recv_msg, vol_pull_sock, 0);
        assert(rc != -1);

        recv_data = zmq_msg_data(&recv_msg);
        op = (img_op_t *)recv_data;

        switch (op->cmd) {
            case IMG_OP_READ:
                rc = pread(fd, buf, op->len, op->off);
                assert(rc == op->len);
                break;

            case IMG_OP_WRITE:
                memcpy((void *)buf, recv_data + sizeof(img_op_t), op->len);
                rc = pwrite(fd, buf, op->len, op->off);
                assert(rc == op->len);
                break;

            default:
                printf("No support!\n");
                assert(0);
                break;
        }

        op->ret = 0;
        rc = zmq_msg_init_size(&send_msg, sizeof(img_op_t) + op->len);
        send_data = zmq_msg_data(&send_msg);
        memcpy(send_data, (void *)op, sizeof(img_op_t));
        if ((op->cmd & IMG_OP_TYPE_GET_DATA) && (op->len > 0)) {
            memcpy(send_data + sizeof(img_op_t), (void *)buf, op->len);
        }

        printf("volume recv tid_t=%ld op\n", op->seq);
        rc = zmq_msg_send(&send_msg, vol_push_sock, 0);
        assert(rc == (sizeof(img_op_t) + op->len));

        rc = zmq_msg_close(&recv_msg);
        assert(rc == 0);
        rc = zmq_msg_close(&send_msg);
        assert(rc == 0);
    }
}
void aio_callback_func(img_aio_comp_t *comp, void *arg)
{
    printf("run callback func for tid_t=%ld op\n", comp->op.seq);
}


int main()
{
    pthread_t volume_td;
    void *zm_ctx = zmq_ctx_new();
    char buf[4096];

    s_sleep(100);
    pthread_create(&volume_td, NULL, file_volume_func, zm_ctx);
    s_sleep(100);

    img_aio_ctx_t *ctx;
    img_aio_comp_t *comp;
    ctx = img_aio_setup();
    s_sleep(1000);

    printf("the game is running\n");

    int i = 0;
    int ret = 0;
    char cc = 'a';
    for(i = 0; i < 2000; i++)
    {
        printf("exec %d async write op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        memset(buf, cc, 4096);
        cc++;
        img_aio_write(comp, 0, buf, 4096, i * 4096);
        img_aio_wait_for_cb(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d write op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
    }

    for (; i< 4000; i++) {
        printf("exec %d sync write op\n", i);
        memset(buf, cc, 4096);
        cc++;
        ret = img_write(ctx, 1, buf, 4096, i * 4096);
        printf("%d write op finished, ret=%d\n", i, ret);
    }

    cc = 'a';
    for(i = 0; i < 2000; i++)
    {
        printf("exec %d async read op\n", i);
        comp = img_aio_create_completion(ctx, aio_callback_func, NULL);
        img_aio_read(comp, 0, buf, 4096, i * 4096);
        img_aio_wait_for_cb(comp);
        ret = img_aio_get_return_value(comp);
        printf("%d write op finished, ret=%d\n", i, ret);
        img_aio_release(comp);
        assert(cc == buf[0]);
        cc++;
    }

    for (; i< 4000; i++) {
        printf("exec %d sync read op\n", i);
        ret = img_read(ctx, 1, buf, 4096, i * 4096);
        printf("%d write op finished, ret=%d\n", i, ret);
        assert(cc == buf[0]);
        cc++;
    }



    s_sleep(1000);
    return 0;
}
