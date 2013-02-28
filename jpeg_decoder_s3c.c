#if defined(S3C_JPG)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <cam/list.h>
#include <cam/utils.h>
#include <cam/jpg.h>


#include <cam/s3c/JPGApi.h>

#if defined(DBG_JPG)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct buf {
	void    *start;
	long     len;
};

struct jpg_dec {
    int             fd;
    int             dw, dh;     /* 解码后图片的宽度和高度 */
    int             sm;         /* Sample mode */     
    struct buf      in_buf, out_buf;
};

static inline int jpg_dec_init(jpg_dec_t dec) {
    struct jpg_dec  *d = dec; 

	/* JPEG Handle initialization */
	d->fd = SsbSipJPEGDecodeInit();
	if (d->fd < 0) {
        pr_debug("JPEG Handle initialization fail."); 
		return -1;
    }

	return 0;
}

static inline void jpg_dec_uninit(jpg_dec_t dec) {
    struct jpg_dec  *d = dec; 
    SsbSipJPEGDecodeDeInit(d->fd);
}

jpg_dec_t jpg_dec_create() 
{
    struct jpg_dec *d = calloc(1, sizeof(struct jpg_dec));
    if (!d) {
		perror("jpg_dec_create");
		return NULL;
	}

	if (jpg_dec_init(d)) 
		goto err_mem;	
    
	return d;
err_mem:
    free(d);
    return NULL;
}

void jpg_dec_free(jpg_dec_t dec) 
{
    struct jpg_dec  *d = dec; 
    jpg_dec_uninit(d);
    free(d);
}

int jpg_dec_frame(jpg_dec_t dec, const void *jpg_frm, int len)
{
    struct jpg_dec  *d = dec; 
    int ret;

    d->in_buf.len = len;  
    d->in_buf.start = SsbSipJPEGGetDecodeInBuf(d->fd, d->in_buf.len);
    if(d->in_buf.start == NULL) {
        pr_debug("SsbSipJPEGGetDecodeInBuf fail."); 
        return -1;
    }

    /* Put JPEG frame to Input buffer */
    memcpy(d->in_buf.start, jpg_frm, d->in_buf.len);

	/* Decode JPEG frame */
	if ((ret = SsbSipJPEGDecodeExe(d->fd)) != JPEG_OK) {
        pr_debug("SsbSipJPEGDecodeExe fail."); 
        return -1;
    }

	/* Get output buffer address */
	d->out_buf.start = SsbSipJPEGGetDecodeOutBuf(d->fd, &d->out_buf.len);
    if(d->out_buf.start == NULL) {
        pr_debug("SsbSipJPEGGetDecodeOutBuf fail."); 
        return -1;
    }

	/* Get decode config. */
    if((ret = SsbSipJPEGGetConfig(JPEG_GET_SAMPING_MODE, &d->sm)) != JPEG_OK) {
        pr_debug("SsbSipJPEGGetConfig JPEG_GET_SAMPING_MODE fail."); 
        return -1;
    }

    if (d->sm != JPG_422) {
        pr_debug("decoder jpg sample mode is %d, rather than "
                 "%d(JPG_422), which is not supported.", d->sm, JPG_422); 
    }

    if((ret = SsbSipJPEGGetConfig(JPEG_GET_DECODE_WIDTH, &d->dw)) != JPEG_OK) {
        pr_debug("SsbSipJPEGGetConfig JPEG_GET_DECODE_WIDTH fail."); 
        return -1;
    }

    if((ret = SsbSipJPEGGetConfig(JPEG_GET_DECODE_HEIGHT, &d->dh)) != JPEG_OK) {
        pr_debug("SsbSipJPEGGetConfig JPEG_GET_DECODE_HEIGHT fail."); 
        return -1;
    }

    return 0;
}

void *jpg_dec_get_outbuf(jpg_dec_t dec, int *len)
{
    struct jpg_dec  *d = dec;
    *len = d->out_buf.len;
    return d->out_buf.start; 
}

void jpg_dec_get_frmsiz(jpg_dec_t dec, int *w, int *h)
{
    struct jpg_dec  *d = dec;
    *w = d->dw;
    *h = d->dh;
}

#endif /* S3C_JPG */

