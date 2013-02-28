/*
 * YUV422 to JPG
 */
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

struct jpg_enc {
    int             fd;
    int             cw, ch;     /* 编码图片的宽度和高度 */
    int             sm;         /* Sample mode */     
    struct buf      in_buf, out_buf;
};

static int jpg_enc_init(jpg_enc_t enc)
{
    struct jpg_enc  *e = enc; 
    int ret;

    pr_debug("\n");
	/* JPEG Handle initialization */
	e->fd = SsbSipJPEGEncodeInit();
	if (e->fd < 0) {
        pr_debug("JPEG Handle initialization fail."); 
		return -1;
    }

    e->sm = JPG_422;        /* 目前只支持这种格式 */
    if((ret = SsbSipJPEGSetConfig(JPEG_SET_SAMPING_MODE, e->sm)) != JPEG_OK) {
        pr_debug("SsbSipJPEGSetConfig JPEG_SET_SAMPING_MODE fail."); 
		goto err_init;
    }

    if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_QUALITY, JPG_QUALITY_LEVEL_2)) != JPEG_OK) {
        pr_debug("SsbSipJPEGSetConfig JPEG_SET_ENCODE_QUALITY fail."); 
		goto err_init;
    }

	return 0;
err_init:
    SsbSipJPEGEncodeDeInit(e->fd);
    return -1;
}

static inline void jpg_enc_uninit(jpg_enc_t enc) {
    struct jpg_enc  *e = enc; 
    SsbSipJPEGEncodeDeInit(e->fd);
}

jpg_enc_t jpg_enc_create() 
{
    struct jpg_enc *e = calloc(1, sizeof(struct jpg_enc));
    if (!e) {
		perror("jpg_enc_create");
		return NULL;
	}

	if (jpg_enc_init(e)) 
		goto err_mem;	
    
	return e;
err_mem:
    free(e);
    return NULL;
}

void jpg_enc_free(jpg_enc_t enc) 
{
    struct jpg_enc  *e = enc; 
    jpg_enc_uninit(e);
    free(e);
}

int jpg_enc_yuyv_frame(jpg_enc_t enc, const void *frm, int w, int h)
{
    struct jpg_enc  *e = enc; 
    int ret;
    bool siz_change = false;

    if (w != e->cw) {
        if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_WIDTH, w)) != JPEG_OK) {
            pr_debug("SsbSipJPEGSetConfig JPEG_SET_ENCODE_WIDTH fail."); 
            return -1;
        }
        e->cw = w; 
        siz_change = true;
    }

    if (h != e->ch) {
        if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_HEIGHT, h)) != JPEG_OK) {
            pr_debug("SsbSipJPEGSetConfig JPEG_SET_ENCODE_HEIGHT fail."); 
            return -1;
        }
        e->ch = h; 
        siz_change = true;
    }

    if (siz_change) {
        e->in_buf.len = e->cw * e->ch * 2;          /* YUV422 */ 
        e->in_buf.start = SsbSipJPEGGetEncodeInBuf(e->fd, e->in_buf.len);
        if(e->in_buf.start == NULL) {
            pr_debug("SsbSipJPEGGetEncodeInBuf fail."); 
            return -1;
        }
    }

    /* Copy YUV data from camera to JPEG driver */
    memcpy(e->in_buf.start, frm, e->in_buf.len);

	/* Encode YUV stream, without ExifInfo */
	if ((ret = SsbSipJPEGEncodeExe(e->fd, NULL, JPEG_USE_SW_SCALER)) != JPEG_OK) {
        pr_debug("SsbSipJPEGEncodeExe fail."); 
        return -1;
    }

	/* Get output buffer address */
	e->out_buf.start = SsbSipJPEGGetEncodeOutBuf(e->fd, &e->out_buf.len);
    if(e->out_buf.start == NULL) {
        pr_debug("SsbSipJPEGGetEncodeOutBuf fail."); 
        return -1;
    }

    return 0;
}

void *jpg_enc_get_outbuf(jpg_enc_t enc, int *len)
{
    struct jpg_enc  *e = enc;
    *len = e->out_buf.len;
    return e->out_buf.start; 
}

#endif /* S3C_JPG */

