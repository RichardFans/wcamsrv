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

struct jpg_enc {
    struct jpeg_compress_struct     cinfo;
    struct jpeg_error_mgr           jerr;
    unsigned char                   *out_buf; 
    unsigned long                   len; 
};

static inline int jpg_enc_init(struct jpg_enc *e) {
    e->cinfo.err = jpeg_std_error(&e->jerr);
    jpeg_create_compress(&e->cinfo);
	return 0;
}

static inline void jpg_enc_uninit(struct jpg_enc *e) {
    jpeg_destroy_compress(&e->cinfo);
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
    JSAMPIMAGE      buffer;
    int  block_width, block_height, block_size[3]; 
    int  i, yi, ui, vi, max_line;
    const __u8  *psrc  = frm; 
    __u8  *tmp;
    
    jpeg_mem_dest(&e->cinfo, &e->out_buf, &e->len);

    e->cinfo.image_width        = w;
    e->cinfo.image_height       = h;
    e->cinfo.input_components   = 3;      /* of color components per pixel */
    e->cinfo.in_color_space     = JCS_YCbCr;  /* colorspace of input image */

    jpeg_set_defaults(&e->cinfo);
    jpeg_set_quality(&e->cinfo, 80, TRUE);
    e->cinfo.raw_data_in = TRUE;
    e->cinfo.comp_info[0].h_samp_factor = 2;
    e->cinfo.comp_info[0].v_samp_factor = 1;

    jpeg_start_compress(&e->cinfo, TRUE);

    buffer = (JSAMPIMAGE)(*e->cinfo.mem->alloc_small)((j_common_ptr)&e->cinfo, 
                                                      JPOOL_IMAGE, 3*sizeof(JSAMPARRAY));

    for(i = 0; i < 3; i++) {
        block_width  = e->cinfo.comp_info[i].width_in_blocks * DCTSIZE;
        block_height = e->cinfo.comp_info[i].v_samp_factor * DCTSIZE;
        block_size[i] = block_width * block_height;
        buffer[i] = (*e->cinfo.mem->alloc_sarray)((j_common_ptr)&e->cinfo, 
                                    JPOOL_IMAGE, block_width, block_height);
    }
    yi = 0, ui = 0, vi = 0;
    max_line = e->cinfo.max_v_samp_factor * DCTSIZE;
    while (e->cinfo.next_scanline < e->cinfo.image_height) {
        /* Y */
        tmp = (__u8*)buffer[0][0];
        for (i = 0; i < block_size[0]; i++, yi++) 
            tmp[i] = psrc[2*yi];

        /* U */
        tmp = (__u8*)buffer[1][0];
        for (i = 0; i < block_size[1]; i++, ui++) 
            tmp[i] = psrc[4*ui + 1];

        /* V */
        tmp = (__u8*)buffer[2][0];
        for (i = 0; i < block_size[2]; i++, vi++) 
            tmp[i] = psrc[4*vi + 3];
        
        jpeg_write_raw_data(&e->cinfo, buffer, max_line);
    }

    jpeg_finish_compress(&e->cinfo);
    return 0;
}

void *jpg_enc_get_outbuf(jpg_enc_t enc, int *len)
{
    struct jpg_enc  *e = enc;
    *len = e->len;
    return e->out_buf; 
}

#if 0
#include <cam/v4l2.h>
#include <cam/app.h>

void img_proc(const void *p, int size, void *arg)
{
    jpg_enc_t enc = arg;
    static int i;
    char buf[32];
    FILE *fp;
    sprintf(buf, "tmp/%d.jpg", ++i);
    fp = fopen(buf, "wb");
    jpg_enc_yuyv_frame(enc, p, 640, 480);
    p = jpg_enc_get_outbuf(enc, &size);
    fwrite(p, size, 1, fp);
    fclose(fp);
    fprintf(stdout, ".");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    app_t a = app_create(0);
    jpg_enc_t enc = jpg_enc_create();

    v4l2_dev_t v = v4l2_create(a, NULL, 0, 0);
    v4l2_set_img_proc(v, img_proc, enc);
    v4l2_start_capture(v);

    app_exec(a);

    v4l2_stop_capture(v);
    v4l2_free(v);
    jpg_enc_free(enc);

    app_free(a);

    return 0;
}
#endif

#endif 

