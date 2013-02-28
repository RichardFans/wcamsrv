#if defined(S3C_FB)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>

#include <cam/list.h>
#include <cam/utils.h>
#include <cam/fbd.h>


#include <cam/s3c/lcd.h>
#include <cam/s3c/post.h>

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
    int             fb_fd;
    struct buf      fb_buf;
    s3c_win_info_t	fb_info;

    int             pp_fd;
    struct buf      pp_buf;
    s3c_pp_params_t	pp_param;
};

static int fb_init(fbd_t fb, int wn, int bpp, int x, int y, int w, int h)
{
    struct fb_disp  *f = fb; 
    char            *fbdev = FB_DEV_NAME;

	switch (wn) {
		case 0:
			fbdev = FB_DEV_NAME;
			break;
		case 1:
			fbdev = FB_DEV_NAME1;
			break;
		case 2:
			fbdev = FB_DEV_NAME2;
			break;
		case 3:
			fbdev = FB_DEV_NAME3;
			break;
		case 4:
			fbdev = FB_DEV_NAME4;
			break;
		default:
			pr_debug("Window number is wrong\n");
			return -1;
	}

    f->fb_fd = open(fbdev, O_RDWR);
	if (f->fb_fd < 0) {
		perror(fbdev);
		return -1;
	}

    switch (bpp) {
    case 16:
        f->fb_buf.len= w * h * 2;	
        break;
    case 24:
        f->fb_buf.len= w * h * 4;
        break;
    default:
        pr_debug("16 and 24 bpp support");
        goto err_open;
    }

	if ((f->fb_buf.start = mmap(NULL, f->fb_buf.len, PROT_READ | PROT_WRITE, 
                               MAP_SHARED, f->fb_fd, 0)) < 0) {
		perror("mmap() in fb_init()");
		goto err_open;
	}

	f->fb_info.Bpp 		    = bpp;
	f->fb_info.LeftTop_x	= x;
	f->fb_info.LeftTop_y	= y;
	f->fb_info.Width 	    = w;
	f->fb_info.Height 	    = h;

	if (ioctl(f->fb_fd, SET_OSD_INFO, &f->fb_info)) {
		pr_debug("Some problem with the ioctl SET_OSD_INFO!!!\n");
		goto err_map;
	}

	if (ioctl(f->fb_fd, SET_OSD_START)) {
		pr_debug("Some problem with the ioctl SET_OSD_START!!!\n");
		goto err_map;
	}

	return 0;
err_map:
    munmap(f->fb_buf.start, f->fb_buf.len);
err_open:
    close(f->fb_fd);
    return -1;
}

static void fb_uninit(fbd_t fb)
{
    struct fb_disp  *f = fb; 
	if (ioctl(f->fb_fd, SET_OSD_STOP)) 
		pr_debug("Some problem with the ioctl SET_OSD_STOP!!!\n");
    if (-1 == munmap(f->fb_buf.start, f->fb_buf.len))
		perror("fb_uninit: munmap\n");
    close(f->fb_fd);
}

static int pp_init(fbd_t fb)
{
    struct fb_disp          *f = fb; 
	struct s3c_fb_dma_info	fb_info;

	/* Post processor open */
	f->pp_fd = open(PP_DEV_NAME, O_RDWR|O_NDELAY);
	if (f->pp_fd < 0) {
		printf("post processor open error\n");
        return -1;
	}

	/* Get post processor's input buffer address */
	f->pp_buf.len   = ioctl(f->pp_fd, S3C_PP_GET_RESERVED_MEM_SIZE);
	f->pp_buf.start = mmap(NULL, f->pp_buf.len, PROT_READ | PROT_WRITE, 
                           MAP_SHARED, f->pp_fd, 0);
	if(f->pp_buf.start == NULL) {
		pr_debug("Post processor mmap failed\n");
		goto err_open;
	}

	/* Set post processor */
	f->pp_param.src_full_width		= f->fb_info.Width;
	f->pp_param.src_full_height	    = f->fb_info.Height;
	f->pp_param.src_start_x		    = 0;
	f->pp_param.src_start_y		    = 0;
	f->pp_param.src_width			= f->pp_param.src_full_width;
	f->pp_param.src_height			= f->pp_param.src_full_height;
	f->pp_param.src_color_space	    = YCBYCR;
	f->pp_param.dst_start_x		    = 0;
	f->pp_param.dst_start_y		    = 0;
    f->pp_param.dst_full_width		= f->fb_info.Width;
    f->pp_param.dst_full_height	    = f->fb_info.Height;
	f->pp_param.dst_width		    = f->pp_param.dst_full_width;
	f->pp_param.dst_height		    = f->pp_param.dst_full_height;	
    if (f->fb_info.Bpp == 16)
        f->pp_param.dst_color_space	= RGB16;
    else
        f->pp_param.dst_color_space	= RGB24;
    f->pp_param.out_path		    = 0;
	f->pp_param.scan_mode		    = 0;

	if (ioctl(f->fb_fd, GET_FB_INFO, &fb_info)) {
		pr_debug("Some problem with the ioctl GET_FB_INFO!!!\n");
		goto err_map;
	}

	f->pp_param.src_buf_addr_phy    = ioctl(f->pp_fd, S3C_PP_GET_RESERVED_MEM_ADDR_PHY);
	f->pp_param.dst_buf_addr_phy    = fb_info.map_dma_f1; 

	if (ioctl(f->pp_fd, S3C_PP_SET_PARAMS, &f->pp_param)) {
		pr_debug("Some problem with the ioctl S3C_PP_SET_PARAMS!!!\n");
		goto err_map;
	}

	if (ioctl(f->pp_fd, S3C_PP_SET_DST_BUF_ADDR_PHY, &f->pp_param)) {
		pr_debug("Some problem with the ioctl S3C_PP_SET_DST_BUF_ADDR_PHY!!!\n");
		goto err_map;
	}

	if (ioctl(f->pp_fd, S3C_PP_SET_SRC_BUF_ADDR_PHY, &f->pp_param)) {
		pr_debug("Some problem with the ioctl S3C_PP_SET_SRC_BUF_ADDR_PHY!!!\n");
		goto err_map;
	}

    return 0;
err_map:
    munmap(f->pp_buf.start, f->pp_buf.len);
err_open:
    close(f->pp_fd);
    return -1;
}

static void pp_uninit(fbd_t fb)
{
    struct fb_disp  *f = fb; 
    if (-1 == munmap(f->pp_buf.start, f->pp_buf.len))
		perror("pp_uninit: munmap\n");
    close(f->pp_fd);
}

fbd_t fbd_create(int wn, int bpp, int x, int y, int w, int h) 
{
    struct fb_disp *f = calloc(1, sizeof(struct fb_disp));
    if (!f) {
		perror("fbd_create");
		return NULL;
	}

    /* LCD frame buffer initialization */
	if (fb_init(f, wn, bpp, x, y, w, h)) 
		goto err_mem;	
    
	/* Post processor open */
	if (pp_init(f)) 
		goto err_fbd;	

	return f;
err_fbd:
    fb_uninit(f);
err_mem:
    free(f);
    return NULL;
}

void fbd_free(fbd_t fb)
{
    struct fb_disp  *f = fb; 
    pp_uninit(f);
    fb_uninit(f);
    free(f);
}

int fbd_show_yuv_frame(fbd_t fb, const void *yuv_frm, int w, int h)
{
    struct fb_disp *f = fb;
    bool param_change = false;

    if (w > 0 && w != f->pp_param.src_full_width) {
        f->pp_param.src_full_width		= w; 
        f->pp_param.src_width			= f->pp_param.src_full_width;

        if (w < f->fb_info.Width) {
            f->pp_param.dst_start_x	    = (f->fb_info.Width - w) / 2;
            f->pp_param.dst_width       = w;
        } else {
            f->pp_param.dst_start_x	    = 0;
            f->pp_param.dst_width       = f->fb_info.Width;
        }

        param_change = true;
    }

    if (h > 0 && h != f->pp_param.src_full_height) {
        f->pp_param.src_full_height	    = h;
        f->pp_param.src_height			= f->pp_param.src_full_height;       

        if (h < f->fb_info.Height) {
            f->pp_param.dst_start_y	    = (f->fb_info.Height - h) / 2;
            f->pp_param.dst_height      = h;
        } else {
            f->pp_param.dst_start_y	    = 0;
            f->pp_param.dst_height      = f->fb_info.Height;
        }

        param_change = true;
    } 

    if (param_change) {
        if (-1 == ioctl(f->pp_fd, S3C_PP_SET_PARAMS, &f->pp_param)) {
            pr_debug("Some problem with the ioctl S3C_PP_SET_PARAMS!!!\n");
            return -1;
        }
    }

	memcpy(f->pp_buf.start, yuv_frm, w * h * 2);
	ioctl(f->pp_fd, S3C_PP_START);
    return 0;
}

#endif /* S3C_FB */

