#ifndef AIO_H
#define AIO_H

#include <stdint.h>
#include <pthread.h>

#define MAX_AIO_NUM         1024
#define MAX_IMG_NAME_SIZE   256

typedef uint64_t tid_t;
typedef uint64_t img_id_t;

typedef enum {
    IMG_OP_TYPE_READ = 0,
    IMG_OP_TYPE_WRITE,
    IMG_OP_TYPE_DISCARD,
    IMG_OP_TYPE_FLUSH,
    IMG_OP_TYPE_CREATE,
    IMG_OP_TYPE_OPEN,
    IMG_OP_TYPE_CLOSE,
    IMG_OP_TYPE_STAT,
    IMG_OP_TYPE_NONE,
}img_op_type_t;

typedef enum {
    IMG_AIO_STAT_READY = 0,
    IMG_AIO_STAT_FLIGHT,
    IMG_AIO_STAT_ACK,
    IMG_AIO_STAT_CALLBACKED,
    IMG_AIO_STAT_RELEASED,
    IMG_AIO_STAT_NONE,
}img_aio_stat_t;

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
    img_id_t img;
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

struct img_aio_context_t {
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
    char img_name[MAX_IMG_NAME_SIZE];
} img_info_t;


// async operates
img_aio_context_t *img_aio_setup();
int img_aio_destroy(img_aio_context_t *ctx);

aio_comp_t * img_aio_create_completion(img_aio_context_t *ctx, img_callback_t cb, void *arg);

int img_aio_create(img_aio_comp_t *comp, char *name, size_t size, int block_order);
img_id_t img_aio_open(img_aio_comp_t *comp, char *name);
int img_aio_close(img_aio_comp_t *comp, img_id_t img);
int img_aio_delete(img_aio_comp_t *comp, img_id_t img);


int img_aio_write(img_aio_comp_t *comp, img_id_t img, char *buf, size_t len, uint64_t off);
int img_aio_read(img_aio_comp_t *comp, img_id_t img, char *buf, size_t len, uint64_t off);
int img_aio_stat(img_aio_comp_t *comp, img_id_t img, char *info, int len);

void img_aio_wait_for_complete(img_aio_comp_t *comp);
void img_aio_wait_for_callbacked(img_aio_comp_t *comp);
int img_aio_is_completion(img_aio_comp_t *comp);
int img_aio_is_completion_and_cb(img_aio_comp_t *comp);
int img_aio_get_return_value(img_aio_comp_t *comp);
int img_aio_release(img_aio_comp_t *comp);

//sync operates
int img_create(img_aio_context_t *ctx, char *name, size_t size, int block_order);
img_id_t img_open(img_aio_context_t *ctx, char *name);
int img_close(img_aio_context_t *ctx, img_id_t img);
int img_delete(img_aio_context_t *ctx, img_id_t img);

int img_write(img_aio_context_t *ctx, img_id_t img, char *buf, size_t len, uint64_t off);
int img_read(img_aio_context_t *ctx, img_id_t img, char *buf, size_t len, uint64_t off);
int img_stat(img_aio_context_t *ctx, img_id_t img, char *info, int len);

#endif
