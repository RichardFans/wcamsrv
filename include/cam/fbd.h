#ifndef __FBD_H__
#define __FBD_H__

typedef struct fb_disp *fbd_t;

#define DEF_FB_DEV      "/dev/fb0"
#define DEF_FB_BPP      16
#define DEF_FB_WIDTH    480
#define DEF_FB_HEIGHT   272

fbd_t fbd_create(int wn, int bpp, int x, int y, int w, int h);
void fbd_free(fbd_t fb);
int fbd_show_yuv_frame(fbd_t fb, const void *yuv_frm, int w, int h);

#endif

