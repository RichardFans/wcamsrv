#if !defined(S3C_FB)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <linux/fb.h>

#include <cam/list.h>
#include <cam/utils.h>
#include <cam/fbd.h>

#if defined(DBG_FBD)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct buf {
	void    *start;
	int     len;
};

struct fb_disp {
    int                         fd;
    struct buf                  fb_buf;
    struct fb_var_screeninfo    vinfo;
    unsigned char               *rgbbuf;
    unsigned char               *zoombuf;
};

static int fb_init(fbd_t fb, int bpp, int x, int y, int w, int h)
{
    struct fb_disp  *f = fb; 
    char            *fbdev = DEF_FB_DEV;

    f->fd = open(fbdev, O_RDWR);
	if (f->fd < 0) {
		perror(fbdev);
		return -1;
	}

    if (bpp != 24 && bpp != 16) {
        pr_debug("just 16 and 24 bpp support, auto select 16 bpp\n");
        bpp = 16;
    }

    /* Get variable screen information */
    if (-1 == ioctl(f->fd, FBIOGET_VSCREENINFO, &f->vinfo)) {
		perror("FBIOGET_VSCREENINFO");
        goto err_open;
    }

    pr_debug("vinfo: xres = %d, yres = %d, bpp = %d\n", 
             f->vinfo.xres, f->vinfo.yres, f->vinfo.bits_per_pixel);

    if (f->vinfo.bits_per_pixel != bpp || f->vinfo.xres != w || 
                                          f->vinfo.yres != h) {
        f->vinfo.bits_per_pixel = bpp;
        f->vinfo.xres = w;
        f->vinfo.yres = h;
        if (-1 == ioctl(f->fd, FBIOPUT_VSCREENINFO, &f->vinfo)) {
            perror("FBIOPUT_VSCREENINFO");
            goto err_open;
        }     

        if (-1 == ioctl(f->fd, FBIOGET_VSCREENINFO, &f->vinfo)) {
            perror("FBIOGET_VSCREENINFO");
            goto err_open;
        }
        pr_debug("vinfo reset to xres = %d, yres = %d, bpp = %d\n", 
                 f->vinfo.xres, f->vinfo.yres, f->vinfo.bits_per_pixel);
    }

    f->fb_buf.len = f->vinfo.xres * f->vinfo.yres * f->vinfo.bits_per_pixel / 8;	
	if ((f->fb_buf.start = mmap(NULL, f->fb_buf.len, PROT_READ | PROT_WRITE, 
                                MAP_SHARED, f->fd, 0)) < 0) {
		perror("mmap() in fb_init()");
		goto err_open;
	}

    f->rgbbuf = malloc(f->fb_buf.len * 2);
    if (f->rgbbuf == NULL) {
		perror("rgbbuf malloc");
        goto err_map;
    }

	return 0;
err_map:
    munmap(f->fb_buf.start, f->fb_buf.len);
err_open:
    close(f->fd);
    return -1;
}

static inline void fb_uninit(struct fb_disp *f) {
    free(f->rgbbuf);
    if (-1 == munmap(f->fb_buf.start, f->fb_buf.len))
		perror("fb_uninit: munmap\n");
    close(f->fd);
}

fbd_t fbd_create(int wn, int bpp, int x, int y, int w, int h) 
{
    struct fb_disp *f = calloc(1, sizeof(struct fb_disp));
    if (!f) {
		perror("fbd_create");
		return NULL;
	}

    /* LCD frame buffer initialization */
	if (fb_init(f, bpp, x, y, w, h)) 
		goto err_mem;	
    
	return f;
err_mem:
    free(f);
    return NULL;
}

void fbd_free(fbd_t fb)
{
    struct fb_disp  *f = fb; 
    if (f->zoombuf != NULL)
        free(f->zoombuf);
    fb_uninit(f);
    free(f);
}

/* just support rgb565 now */
void show_rgb16_frame(struct fb_disp *f, int x0, int y0, int w, int h)
{
    __u16 *pdst = f->fb_buf.start;
    __u8  *psrc = f->rgbbuf; 
    int xres    = f->vinfo.xres;
    int x1      = w + x0;
    int y1      = h + y0;
    int x, y;
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            pdst[y*xres+x] = ((psrc[0]>>3)<<11) |
                             ((psrc[1]>>2)<<5)  |
                             ((psrc[2]>>3));
            psrc += 3;
        }
    }
}

void show_rgb24_frame(struct fb_disp *f, int x0, int y0, int w, int h)
{
    __u8  *pdst = f->fb_buf.start;
    __u8  *psrc = f->rgbbuf; 
    int xres    = f->vinfo.xres;
    int y1      = h + y0;
    int y;
    for (y = y0; y < y1; y++) {
        memcpy(&pdst[y*xres+x0], psrc, w * 3);
        psrc += w * 3;
    }
}

void show_rgb32_frame(struct fb_disp *f, int x0, int y0, int w, int h)
{
    __u32  *pdst = f->fb_buf.start;
    __u8   *psrc = f->rgbbuf; 
    int xres    = f->vinfo.xres;
    int x1      = w + x0;
    int y1      = h + y0;
    int x, y;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            pdst[y*xres+x] = (0xFF<<24) |
                             (psrc[0]<<16) |
                             (psrc[1]<<8)  |
                             (psrc[2]);
            psrc += 3;
        }
    }    
}

int fbd_show_yuv_frame(fbd_t fb, const void *yuv_frm, int w, int h)
{
    struct fb_disp *f = fb;
    int dx, dy, dw, dh; 
    bool need_zoom_in = false;
    const __u8 *pfrm = yuv_frm;

#if 1
    if (w <= f->vinfo.xres) {
        dw = w;
        dx = (f->vinfo.xres - dw) / 2;
    } else {
        dx = 0;
        dw = f->vinfo.xres;
        need_zoom_in = true;
    }

    if (h <= f->vinfo.yres) {
        dh = h;
        dy = (f->vinfo.yres - dh) / 2;
    } else {
        dy = 0;
        dh = f->vinfo.yres;
        need_zoom_in = true;
    }
#else
    /* test zoom in/out */
    dw = 480;       /* width for display */
    dx = (f->vinfo.xres - dw) / 2;
    dh = 272;       /* height for display */
    dy = (f->vinfo.yres - dh) / 2;
    need_zoom_in = true;
#endif
    if (need_zoom_in) {
        if (f->zoombuf == NULL)
            f->zoombuf = malloc(w * h * 3);
        convert_yuv422_to_rgb_buffer(pfrm, f->zoombuf, w, h);
        zoom_rgb(f->zoombuf, f->rgbbuf, w, h, (float)dw/w, (float)dh/h);
    } else {
        convert_yuv422_to_rgb_buffer(pfrm, f->rgbbuf, w, h);
    }
    
    if (f->vinfo.bits_per_pixel == 16)
        show_rgb16_frame(f, dx, dy, dw, dh);
    else if (f->vinfo.bits_per_pixel == 24)
        show_rgb24_frame(f, dx, dy, dw, dh);
    else if (f->vinfo.bits_per_pixel == 32)
        show_rgb32_frame(f, dx, dy, dw, dh);

    return 0;
}

#if 0
#include <cam/v4l2.h>
#include <cam/jpg.h>
#include <cam/fbd.h>

jpg_dec_t d;

void img_proc(const void *p, int size, void *arg)
{
    fbd_t f = arg;
    int w, h; 
    jpg_dec_frame(d, p, size);
    p = jpg_dec_get_outbuf(d, &size);
    jpg_dec_get_frmsiz(d, &w, &h);
    fbd_show_yuv_frame(f, p, w, h);

    fprintf(stdout, ".");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    app_t a = app_create(0);

    fbd_t f = fbd_create(0, 24, 0, 0, 1280, 1024);
    v4l2_dev_t v = v4l2_create(a, NULL, 0, 0);
    v4l2_set_img_proc(v, img_proc, f);
    v4l2_start_capture(v);
    d = jpg_dec_create();

    app_exec(a);

    jpg_dec_free(d);
    v4l2_stop_capture(v);
    v4l2_free(v);
    fbd_free(f);

    app_free(a);

    return 0;
}
#endif

#endif /* S3C_FB */

