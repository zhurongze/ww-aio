#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <zmq.h>
#include <stdio.h>

#include "ww_aio.h"

static void *dispatch_td_func(void *arg)
{
    img_aio_ctx_t *ctx = (img_aio_ctx_t *)arg;
    img_op_t *op;
    img_aio_comp_t *comp;
    zmq_msg_t msg;
    void *data = NULL;
    int rc = 0;
    while(1)
    {
        rc = zmq_msg_init(&msg);
        assert(rc == 0);
        rc = zmq_msg_recv(&msg, ctx->pull_sock, 0);
        assert(rc != -1);

        data = zmq_msg_data(&msg);
        op = (img_op_t *)data;
        comp = op->comp;

        pthread_mutex_lock(&(comp->lock));

        comp->op.ret = op->ret;
        if ((op->cmd & IMG_OP_TYPE_GET_DATA) && (op->reply_len > 0)) {
            memcpy((void *)(op->buf), data + sizeof(img_op_t), op->reply_len);
        }
        if (comp->cb) {
            comp->cb(comp, comp->cb_arg);
        }
        comp->aio_stat = IMG_AIO_STAT_ACK;

        pthread_cond_signal(&(comp->cond));
        pthread_mutex_unlock(&(comp->lock));

        rc = zmq_msg_close(&msg);
        assert(rc == 0);
    }
}


static tid_t get_new_seq(img_aio_ctx_t *ctx)
{
    pthread_mutex_lock(&(ctx->lock));
    tid_t seq = ctx->op_seq++;
    pthread_mutex_unlock(&(ctx->lock));
    return seq;
}

static img_aio_comp_t *get_aio_comp(img_aio_ctx_t *ctx)
{
    int i = 0, j = 0;
    int found = 0;
    img_aio_comp_t *comp;
    pthread_mutex_lock(&(ctx->lock));

    j = ctx->comp_array_p;
    while(found == 0)
    {
        for( i = 0; i < MAX_AIO_NUM; i++)
        {
            j = (j + 1) % MAX_AIO_NUM;
            if(*(ctx->comp_map + j) == 0)
            {
                *(ctx->comp_map + j) = 0xff;
                comp = ctx->comp_array_base + j;
                ctx->comp_array_p = j;
                found = 1;
                break;
            }
        }
        if(found == 0)
        {
            pthread_cond_wait(&(ctx->cond), &(ctx->lock));
        }
    }
    pthread_mutex_unlock(&(ctx->lock));

    return comp;
}

static int release_aio_comp(img_aio_comp_t *comp)
{
    int p = 0;
    img_aio_ctx_t *ctx = comp->ctx;

    pthread_mutex_lock(&(ctx->lock));

    pthread_mutex_destroy(&comp->lock);
    pthread_cond_destroy(&comp->cond);

    memset(comp, 0, sizeof(img_aio_comp_t));

    p = comp - ctx->comp_array_base;
    *(ctx->comp_map + p) = 0;

    pthread_cond_signal(&(ctx->cond));
    pthread_mutex_unlock(&(ctx->lock));

    return 0;
}

img_aio_ctx_t *img_aio_setup()
{
    int rc = 0;
    img_aio_ctx_t *ctx = NULL;
    ctx = malloc(sizeof(img_aio_ctx_t));
    assert(ctx != NULL);
    memset(ctx, 0, sizeof(img_aio_ctx_t));

    ctx->mq_ctx = zmq_ctx_new();
    assert(ctx != NULL);

    ctx->comp_array_base = malloc(sizeof(img_aio_comp_t) * MAX_AIO_NUM);
    assert(ctx->comp_array_base != NULL);
    memset(ctx->comp_array_base, 0, sizeof(img_aio_comp_t) * MAX_AIO_NUM);
    ctx->comp_map = malloc(MAX_AIO_NUM);
    assert(ctx->comp_map != NULL);
    memset(ctx->comp_map, 0, MAX_AIO_NUM);

    ctx->op_seq = 0;
    ctx->comp_array_p = 0;

    pthread_mutex_init(&(ctx->lock), NULL);
    pthread_cond_init(&(ctx->cond), NULL);

    ctx->pull_sock = zmq_socket(ctx->mq_ctx, ZMQ_PULL);
    rc = zmq_bind(ctx->pull_sock, "tcp://*:9000");
    assert(rc == 0);

    ctx->push_sock = zmq_socket(ctx->mq_ctx, ZMQ_PUSH);
    rc = zmq_connect(ctx->push_sock, "tcp://localhost:9001");
    assert(rc == 0);

    rc = pthread_create(&(ctx->dispatch_td), NULL, dispatch_td_func, (void *)ctx);
    assert(rc == 0);

    return ctx;
}

int img_aio_destroy(img_aio_ctx_t *ctx)
{
    //TODO destroy aio_context and zeromq context
    assert(ctx != NULL);
    pthread_cancel(ctx->dispatch_td);
    pthread_cancel(ctx->callback_td);

    zmq_close(ctx->push_sock);
    zmq_close(ctx->pull_sock);

    zmq_ctx_destroy(ctx->mq_ctx);
    return 0;
}

img_aio_comp_t * img_aio_create_completion(img_aio_ctx_t *ctx, img_callback_t cb, void *arg)
{
    assert(ctx != NULL);
    img_aio_comp_t *comp = get_aio_comp(ctx);
    comp->cb = cb;
    comp->cb_arg = arg;
    comp->ctx = ctx;
    comp->aio_stat = IMG_AIO_STAT_NONE;

    pthread_mutex_init(&(comp->lock), NULL);
    pthread_cond_init(&(comp->cond), NULL);

    return comp;
}

static int build_op(img_op_t *op, int id, char *buf, size_t len, uint64_t off,
                    tid_t seq, img_op_type_t cmd, img_aio_comp_t *comp)
{
    op->id = id;
    op->buf = buf;
    op->len = len;
    op->off = off;
    op->seq = seq;
    op->comp = comp;
    op->ret = 0;
    op->cmd = cmd;

    if (op->cmd & IMG_OP_TYPE_PUT_DATA) {
        op->send_len = op->len;
    }
    op->reply_len = 0;

    return 0;
}

static int img_aio_submit(img_aio_ctx_t *ctx, img_op_t *op)
{
    //zmq_send(ctx->push_sock, (void *)op, sizeof(img_op_t), 0);
    zmq_msg_t msg;

    op->comp->aio_stat = IMG_AIO_STAT_FLIGHT;

    int len = sizeof(img_op_t) + op->send_len;
    int rc = zmq_msg_init_size(&msg, sizeof(img_op_t) + op->send_len);
    assert(rc == 0);

    void *dest = zmq_msg_data(&msg);
    memcpy(dest, (void *)op, sizeof(img_op_t));
    if ((op->cmd & IMG_OP_TYPE_PUT_DATA) && (op->send_len > 0)) {
        memcpy(dest + sizeof(img_op_t), (void *)(op->buf), op->send_len);
    }

    rc = zmq_msg_send(&msg, ctx->push_sock, 0);
    assert(rc == len);

    rc = zmq_msg_close(&msg);
    assert(rc == 0);
    return 0;

}

static void img_aio_wait_for(img_aio_comp_t *comp, img_aio_stat_t stat)
{
    assert(comp != NULL);
    pthread_mutex_lock(&(comp->lock));
    while((comp->aio_stat & stat) != stat)
    {
       pthread_cond_wait(&(comp->cond), &(comp->lock));
    }
    pthread_mutex_unlock(&(comp->lock));
}

void img_aio_wait_for_ack(img_aio_comp_t *comp)
{
    img_aio_wait_for(comp, IMG_AIO_STAT_ACK);
}

int img_aio_is_ack(img_aio_comp_t *comp)
{
    return (comp->aio_stat & IMG_AIO_STAT_ACK);
}

int img_aio_get_return_value(img_aio_comp_t *comp)
{
    return comp->op.ret;
}

int img_aio_release(img_aio_comp_t *comp)
{
    release_aio_comp(comp);
    return 0;
}


//async operates

// create,open,delete operates's img id is SUPER_IMG_ID
int img_aio_create(img_aio_comp_t *comp, img_info_t *img)
{
    assert(comp != NULL);
    assert(img != NULL);


    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), SUPER_IMG_ID, (char *)img, sizeof(img_info_t), 0, seq, IMG_OP_CREATE, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;
}

int img_aio_open(img_aio_comp_t *comp, img_info_t *img)
{
    assert(comp != NULL);
    assert(img != NULL);

    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), SUPER_IMG_ID, (char *)img, sizeof(img_info_t), 0, seq, IMG_OP_OPEN, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;
}

int img_aio_close(img_aio_comp_t *comp, int id)
{
    assert(comp != NULL);

    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), id, NULL, 0, 0, seq, IMG_OP_CLOSE, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;
}

int img_aio_delete(img_aio_comp_t *comp, char *name)
{
    img_info_t img;
    assert(comp != NULL);

    memcpy(img.name, name, strlen(name));
    img.name[strlen(name)] = 0;

    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), SUPER_IMG_ID, (char *)&img, sizeof(img_info_t), 0, seq, IMG_OP_DELETE, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;
}

int img_aio_stat(img_aio_comp_t *comp, img_info_t *img)
{
    assert(comp != NULL);
    assert(img != NULL);

    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), img->id, (char *)img, sizeof(img_info_t), 0, seq, IMG_OP_STAT, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;

}

int img_aio_write(img_aio_comp_t *comp, int id, char *buf, size_t len, uint64_t off)
{
    assert(comp != NULL);
    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), id, buf, len, off, seq, IMG_OP_WRITE, comp);
    img_aio_submit(comp->ctx, &(comp->op));

    return 0;
}

int img_aio_read(img_aio_comp_t *comp, int id, char *buf, size_t len, uint64_t off)
{
    assert(comp != NULL);
    tid_t seq = get_new_seq(comp->ctx);
    build_op(&(comp->op), id, buf, len, off, seq, IMG_OP_READ, comp);
    img_aio_submit(comp->ctx, &(comp->op));
    return 0;
}


//sync operates
int img_create(img_aio_ctx_t *ctx, char *name, size_t size, int block_order)
{
    int ret = 0;
    img_aio_comp_t *comp;
    img_info_t img;

    memcpy(img.name, name, strlen(name));
    img.name[strlen(name)] = 0;
    img.size = size;
    img.order = block_order;

    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_create(comp, &img);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);
    return ret;
}

// return img description
int img_open(img_aio_ctx_t *ctx, char *name)
{
    int ret = 0;
    img_aio_comp_t *comp;
    img_info_t img;

    memcpy(img.name, name, strlen(name));
    img.name[strlen(name)] = 0;
    img.id = 0;

    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_open(comp, &img);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);

    if (ret == 0) {
        return img.id;
    } else {
        return -1;
    }
}

int img_close(img_aio_ctx_t *ctx, int id)
{
    int ret = 0;
    img_aio_comp_t *comp;
    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_close(comp, id);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);
    return ret;
}

int img_delete(img_aio_ctx_t *ctx, char *name)
{
    int ret = 0;
    img_aio_comp_t *comp;
    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_delete(comp, name);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);
    return ret;
}

int img_write(img_aio_ctx_t *ctx, int id, char *buf, size_t len, uint64_t off)
{
    int ret = 0;
    img_aio_comp_t *comp;
    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_write(comp, id, buf, len, off);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);
    return ret;
}

int img_read(img_aio_ctx_t *ctx, int id, char *buf, size_t len, uint64_t off)
{
    int ret = 0;
    img_aio_comp_t *comp;
    comp = img_aio_create_completion(ctx, NULL, NULL);
    img_aio_read(comp, id, buf, len, off);
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);
    img_aio_release(comp);
    return ret;
}

int img_stat(img_aio_ctx_t *ctx, int id, img_info_t *img)
{
    int ret = 0;

    img_aio_comp_t *comp;
    comp = img_aio_create_completion(ctx, NULL, NULL);

    img->id = id;
    img_aio_stat(comp, img);
    
    img_aio_wait_for_ack(comp);
    ret = img_aio_get_return_value(comp);

    img_aio_release(comp);
    return ret;
}
