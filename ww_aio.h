#ifndef AIO_H
#define AIO_H

#include <stdint.h>
#include <pthread.h>

#define MAX_AIO_NUM         1024
#define MAX_IMG_NAME_SIZE   257
#define SUPER_IMG_ID         0

typedef uint64_t tid_t;

#define    IMG_OP_TYPE_NONE        0x0000
#define    IMG_OP_TYPE_PUT_DATA    0x0100
#define    IMG_OP_TYPE_GET_DATA    0x0200
#define    IMG_OP_TYPE_SUPER       0x0400
#define    IMG_OP_READ             (0x01 | IMG_OP_TYPE_GET_DATA)
#define    IMG_OP_WRITE            (0x02 | IMG_OP_TYPE_PUT_DATA)
#define    IMG_OP_DISCARD          (0x03 | IMG_OP_TYPE_NONE)
#define    IMG_OP_FLUSH            (0x04 | IMG_OP_TYPE_NONE)
#define    IMG_OP_CREATE           (0x05 | IMG_OP_TYPE_PUT_DATA | IMG_OP_TYPE_SUPER)
#define    IMG_OP_OPEN             (0x06 | IMG_OP_TYPE_PUT_DATA | IMG_OP_TYPE_GET_DATA | IMG_OP_TYPE_SUPER)
#define    IMG_OP_CLOSE            (0x07 | IMG_OP_TYPE_NONE)
#define    IMG_OP_STAT             (0x08 | IMG_OP_TYPE_PUT_DATA | IMG_OP_TYPE_GET_DATA)
#define    IMG_OP_DELETE           (0x09 | IMG_OP_TYPE_PUT_DATA | IMG_OP_TYPE_SUPER)

typedef  uint32_t img_op_type_t;


#define    IMG_AIO_STAT_NONE            0x00
#define    IMG_AIO_STAT_FLIGHT          0x01
#define    IMG_AIO_STAT_ACK             0x02
#define    IMG_AIO_STAT_CALLBACKED      0x04
#define    IMG_AIO_STAT_RELEASED        0x08

typedef uint32_t img_aio_stat_t;

struct img_op_t;
struct img_aio_comp_t;
struct img_aio_ctx_t;
struct img_info_t;
typedef struct img_op_t img_op_t;
typedef struct img_aio_comp_t img_aio_comp_t;
typedef struct img_aio_ctx_t img_aio_ctx_t;
typedef struct img_info_t img_info_t;

typedef void (*img_callback_t)(img_aio_comp_t *comp, void *arg);

struct img_op_t {
    //img id
    int id;
    char *buf;
    tid_t seq;
    size_t len;
    uint64_t off;
    img_op_type_t cmd;
    int ret;
    img_aio_comp_t *comp;
} __attribute__((packed, aligned(64)));

struct img_aio_comp_t {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    img_aio_stat_t aio_stat;
    img_callback_t cb;
    void *cb_arg;
    img_aio_ctx_t *ctx;
    img_op_t op;
} __attribute__((packed, aligned(64)));

struct img_aio_ctx_t {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    void *mq_ctx;
    void *pull_sock;
    void *push_sock;
    img_aio_comp_t *aios_base;
    int aios_p;
    char *aio_comp_map;
    pthread_t dispatch_td;
    pthread_t callback_td;
    tid_t op_seq;
} __attribute__((packed, aligned(64)));

struct img_info_t {
    uint64_t size;
    uint64_t obj_size;
    uint64_t num_objs;
    int order;
    char name[MAX_IMG_NAME_SIZE];
    uint64_t flag;
    int id;
};


// async operates
img_aio_ctx_t *img_aio_setup();
int img_aio_destroy(img_aio_ctx_t *ctx);

img_aio_comp_t * img_aio_create_completion(img_aio_ctx_t *ctx, img_callback_t cb, void *arg);

int img_aio_create(img_aio_comp_t *comp, img_info_t *img);
int img_aio_open(img_aio_comp_t *comp, img_info_t *img);
int img_aio_close(img_aio_comp_t *comp, int id);
int img_aio_delete(img_aio_comp_t *comp, char *name);


int img_aio_write(img_aio_comp_t *comp, int id, char *buf, size_t len, uint64_t off);
int img_aio_read(img_aio_comp_t *comp, int id, char *buf, size_t len, uint64_t off);
int img_aio_stat(img_aio_comp_t *comp, img_info_t *img);

void img_aio_wait_for_ack(img_aio_comp_t *comp);
void img_aio_wait_for_cb(img_aio_comp_t *comp);
int img_aio_is_ack(img_aio_comp_t *comp);
int img_aio_is_ack_and_cb(img_aio_comp_t *comp);
int img_aio_get_return_value(img_aio_comp_t *comp);
int img_aio_release(img_aio_comp_t *comp);

//sync operates
int img_create(img_aio_ctx_t *ctx, char *name, size_t size, int block_order);
int img_open(img_aio_ctx_t *ctx, char *name);
int img_close(img_aio_ctx_t *ctx, int id);
int img_delete(img_aio_ctx_t *ctx, char *name);

int img_write(img_aio_ctx_t *ctx, int id, char *buf, size_t len, uint64_t off);
int img_read(img_aio_ctx_t *ctx, int id, char *buf, size_t len, uint64_t off);
int img_stat(img_aio_ctx_t *ctx, int id, img_info_t *img);

#endif
