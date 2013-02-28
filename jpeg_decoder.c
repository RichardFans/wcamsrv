#if !defined(S3C_JPG)

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
#include <linux/types.h>

#include <jpeglib.h>

#include <cam/list.h>
#include <cam/utils.h>
#include <cam/jpg.h>

#if defined(DBG_JPG)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct jpg_dec {
    struct jpeg_decompress_struct   cinfo;
    struct jpeg_error_mgr           jerr;
    unsigned char                   out_buf[JPG_BUF_MAX_SIZE];
    unsigned long                   len; 
};

static inline int jpg_dec_init(struct jpg_dec *d) {
    d->cinfo.err = jpeg_std_error(&d->jerr);
    jpeg_create_decompress(&d->cinfo);
	return 0;
}

static inline void jpg_dec_uninit(struct jpg_dec *d) {
    jpeg_destroy_decompress(&d->cinfo);
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

/*
 * jpeg to yuv422 
 */
int jpg_dec_frame(jpg_dec_t dec, const void *jpg_frm, int len)
{
    struct jpg_dec  *d      = dec; 
    __u8            *psrc   = (__u8*)jpg_frm;
    __u8            *pdst   = d->out_buf;
    JSAMPARRAY      line;
    unsigned char   *wline;      /* Will point to line[0] */
    int             linesize, i, width, height;

    jpeg_mem_src(&d->cinfo, psrc, len);

    jpeg_read_header(&d->cinfo, TRUE);
    d->cinfo.out_color_space = JCS_YCbCr;

    jpeg_start_decompress(&d->cinfo);
    /* YCbCr format will give us one byte each for YUV. */
    width  = d->cinfo.output_width;
    height = d->cinfo.output_height;
    linesize = width * 3;

    /* Allocate space for one line. */
    line = (d->cinfo.mem->alloc_sarray)((j_common_ptr)&d->cinfo, JPOOL_IMAGE,
                                        width * d->cinfo.output_components, 1);
    wline = line[0];

    while (d->cinfo.output_scanline < height) {
        jpeg_read_scanlines(&d->cinfo, line, 1);

        for (i = 0; i < linesize; i += 6) {
            *pdst++ = wline[i];     /* Y */
            *pdst++ = wline[i+1];   /* U */
            *pdst++ = wline[i+3];   /* Y */
            *pdst++ = wline[i+5];   /* V */
        }
    }
    d->len = pdst - d->out_buf;

    jpeg_finish_decompress(&d->cinfo);

    return 0;
}

void *jpg_dec_get_outbuf(jpg_dec_t dec, int *len)
{
    struct jpg_dec  *d = dec;
    *len = d->len;
    return d->out_buf; 
}

void jpg_dec_get_frmsiz(jpg_dec_t dec, int *w, int *h)
{
    struct jpg_dec  *d = dec;
    *w = d->cinfo.image_width;
    *h = d->cinfo.image_height;
}

#if 0
#include <cam/v4l2.h>
#include <cam/app.h>

void img_proc(const void *p, int size, void *arg)
{
    jpg_dec_t d = arg;
    static int i;
    char buf[32];
    FILE *fp;
    sprintf(buf, "tmp/%d.yuv", ++i);
    fp = fopen(buf, "w");

    jpg_dec_frame(d, p, size);
    p = jpg_dec_get_outbuf(d, &size);
    fwrite(p, size, 1, fp);
    fclose(fp);
    fprintf(stdout, ".");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    app_t a = app_create(0);
    jpg_dec_t d = jpg_dec_create();

    v4l2_dev_t v = v4l2_create(a, NULL, 0, 0);
    v4l2_set_img_proc(v, img_proc, d);
    v4l2_start_capture(v);

    app_exec(a);

    v4l2_stop_capture(v);
    v4l2_free(v);
    jpg_dec_free(d);

    app_free(a);

    return 0;
}
#endif


#endif 

