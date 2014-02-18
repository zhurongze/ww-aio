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

#define     META_POSTFIX  ".meta"

// 20MB buffer
#define     BUF_SIZE    20971520

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

void get_meta_file_name(const char *filename, char *meta_file_name)
{
    memcpy(meta_file_name, filename, strlen(filename));
    memcpy(meta_file_name + strlen(filename), META_POSTFIX, strlen(META_POSTFIX));
    meta_file_name[strlen(filename) + strlen(META_POSTFIX)] = 0;
}

void *file_volume_func(void *arg)
{
    int rc = 0;
    // zmq initialization
    void *ctx = arg;
    void * vol_pull_sock = NULL;
    void * vol_push_sock = NULL;
    vol_pull_sock = zmq_socket(ctx, ZMQ_PULL);
    vol_push_sock = zmq_socket(ctx, ZMQ_PUSH);

    rc = zmq_bind(vol_pull_sock, "tcp://*:9001");
    assert(rc == 0);
    rc = zmq_connect(vol_push_sock, "tcp://localhost:9000");
    assert(rc == 0);

    zmq_msg_t recv_msg, send_msg;
    char *recv_msg_buf = NULL;
    char *send_msg_buf = NULL;

    // engine initialization
    int fd;
    int fds[MAX_VOLUMES + 1];
    img_info_t imgs[MAX_VOLUMES + 1];
    memset(fds, 0, sizeof(fds));
    memset(imgs, 0, sizeof(imgs));
    char img_meta_name[256];

    char *buf = NULL;
    buf = malloc(BUF_SIZE);
    assert(buf != NULL);

    img_info_t *img;
    img_op_t *op;

    printf("File volume thread is running\n");
    while(1)
    {
        // recv msg
        rc = zmq_msg_init(&recv_msg);
        assert(rc == 0);
        rc = zmq_msg_recv(&recv_msg, vol_pull_sock, 0);
        assert(rc != -1);
        recv_msg_buf = zmq_msg_data(&recv_msg);

        op = (img_op_t *)recv_msg_buf;
        op->reply_len = 0;
        op->ret = SUCCESS;
        assert(op->len <= BUF_SIZE);

        // print op 
        char *command = NULL;
        switch (op->cmd) {
            case IMG_OP_CREATE:
                command = "CREATE";
                break;
            case IMG_OP_OPEN:
                command = "OPEN";
                break;
            case IMG_OP_DELETE:
                command = "DELETE";
                break;
            case IMG_OP_CLOSE:
                command = "CLOSE";
                break;
            case IMG_OP_STAT:
                command = "STAT";
                break;
            case IMG_OP_WRITE:
                command = "WRITE";
                break;
            case IMG_OP_READ:
                command = "READ";
                break;
            default:
                command = "UNKNOW";
                break;
        }
        printf("\nFile_Volume_Thread LOG: receive %s op, seq=%lu, img id=%d, len=%lu, off=%lu\n", command, op->seq, op->id, (uint64_t)op->len, (uint64_t)op->off);

        if ((op->id == 0) && (op->cmd & IMG_OP_TYPE_SUPER)) {
            switch (op->cmd) {
                case IMG_OP_CREATE:
                    // create volume file
                    assert(op->len > 0);
                    img = (img_info_t *)(recv_msg_buf + sizeof(img_op_t));
                    fd = open(img->name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    assert(fd > 0);
                    close(fd);

                    // create volume meta file
                    get_meta_file_name(img->name, img_meta_name);
                    fd = open(img_meta_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    assert(fd > 0);
                    rc = pwrite(fd, (char *)img, sizeof(img_info_t), 0);
                    assert(rc == sizeof(img_info_t));
                    close(fd);

                    op->ret = SUCCESS;
                    op->reply_len = 0;
                    printf("File_Volume_Thread LOG: CREATE OP, name=%s\n", img->name);
                    break;

                case IMG_OP_OPEN:
                    // find slot to store volume descriptor
                    assert(op->len > 0);
                    img = (img_info_t *)(recv_msg_buf + sizeof(img_op_t));
                    fd = open(img->name, O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    assert(fd > 0);
                    int slot = 0;
                    for (slot = 1; slot <= MAX_VOLUMES; slot++) {
                        if (fds[slot] == 0)
                            break;
                    }
                    if (slot == (MAX_VOLUMES + 1)) {
                        printf("Excess Max Volumes!!\n");
                        assert(0);
                    }

                    // store volume description on volume descriptor
                    fds[slot] = fd;
                    get_meta_file_name(img->name, img_meta_name);
                    fd = open(img_meta_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                    assert(fd > 0);
                    rc = pread(fd, (char *)(imgs + slot), sizeof(img_info_t), 0);
                    assert(rc == sizeof(img_info_t));
                    close(fd);
                    imgs[slot].id = slot;
                    memcpy((void *)buf, (char *)(imgs + slot), sizeof(img_info_t));

                    op->ret = SUCCESS;
                    op->reply_len = sizeof(img_info_t);
                    printf("File_Volume_Thread LOG: OPEN OP, name=%s, slot=%d\n", img->name, slot);
                    break;

                case IMG_OP_DELETE:
                    // remove volume file
                    assert(op->len > 0);
                    img = (img_info_t *)(recv_msg_buf + sizeof(img_op_t));
                    rc = remove(img->name);
                    assert(rc == 0);

                    // remove volume meta  file
                    get_meta_file_name(img->name, img_meta_name);
                    rc = remove(img_meta_name);
                    assert(rc == 0);

                    op->ret = SUCCESS;
                    op->reply_len = 0;
                    printf("File_Volume_Thread LOG: DELETE OP, name=%s\n", img->name);
                    break;

                default:
                    printf("No support!\n");
                    op->ret = FAIL;
                    op->reply_len = 0;
                    break;
            }
        } else if ( op->id > 0 && op->id <= MAX_VOLUMES) {
            fd = fds[op->id];

            switch (op->cmd) {
                case IMG_OP_READ:
                    assert(op->len > 0);
                    rc = pread(fd, buf, op->len, op->off);
                    if (rc == 0) {
                        //TODO img virtual size will excess file size
                        printf("File_Volume_Thread LOG: READ OP, return zero data\n");
                        memset(buf, 0, op->len);
                        rc = op->len;
                    }
                    assert(rc == op->len);

                    op->ret = SUCCESS;
                    op->reply_len = rc;
                    printf("File_Volume_Thread LOG: READ OP, slot=%d, len=%d, off=%lu, rc=%d\n", op->id,  (int)op->len, op->off,rc);
                    break;

                case IMG_OP_WRITE:
                    assert(op->len > 0);
                    memcpy((void *)buf, recv_msg_buf + sizeof(img_op_t), op->len);
                    rc = pwrite(fd, buf, op->len, op->off);
                    assert(rc == op->len);

                    op->ret = SUCCESS;
                    op->reply_len = 0;
                    printf("File_Volume_Thread LOG: WRITE OP, slot=%d, len=%d, off=%lu, rc=%d\n", op->id,  (int)op->len, op->off,rc);
                    break;

                case IMG_OP_STAT:
                    memcpy((void *)buf, (void *)(imgs + op->id), sizeof(img_info_t));

                    op->ret = SUCCESS;
                    op->reply_len = sizeof(img_info_t);
                    printf("File_Volume_Thread LOG: STAT OP, name=%s\n", img->name);
                    break;

                case IMG_OP_CLOSE:
                    rc = close(fd);
                    assert(rc == 0);
                    fds[op->id] = 0;
                    memset(imgs + op->id, 0, sizeof(img_info_t));

                    op->ret = SUCCESS;
                    op->reply_len = 0;
                    printf("File_Volume_Thread LOG: CLOSE OP, slot=%d\n", op->id);
                    break;

                default:
                    printf("No support!\n");
                    op->ret = FAIL;
                    op->reply_len = 0;
                    break;
            }
        } else {
            printf("Unknow Command!\n");
            assert(0);
        }

        // send msg
        rc = zmq_msg_init_size(&send_msg, sizeof(img_op_t) + op->reply_len);
        assert(rc == 0);
        send_msg_buf = zmq_msg_data(&send_msg);
        memcpy(send_msg_buf, (void *)op, sizeof(img_op_t));
        if ((op->cmd & IMG_OP_TYPE_GET_DATA) && (op->reply_len > 0)) {
            memcpy(send_msg_buf + sizeof(img_op_t), (void *)buf, op->reply_len);
        }
        rc = zmq_msg_send(&send_msg, vol_push_sock, 0);
        assert(rc == (sizeof(img_op_t) + op->reply_len));

        // close msgs
        rc = zmq_msg_close(&recv_msg);
        assert(rc == 0);
        rc = zmq_msg_close(&send_msg);
        assert(rc == 0);
    }
}

void aio_callback_func(img_aio_comp_t *comp, void *arg)
{
    printf("Callback func\n");

}


int main()
{
    pthread_t volume_td;
    void *zm_ctx = zmq_ctx_new();
    assert(zm_ctx != NULL);
    pthread_create(&volume_td, NULL, file_volume_func, zm_ctx);
    s_sleep(5000000);
    return 0;
}
