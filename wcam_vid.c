#if defined (VID_FUNC)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include <pthread.h>

#include "wcam_priv.h"

#if defined(DBG_VID)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct buf {
	void            *start;
	int             len;
};

struct vid {
    v4l2_dev_t              cam; 
    struct buf              tran_frm;           /* frame to transfer */
    __u32                   tran_frm_max_size;
    __u64                   tran_frm_index;     /* 帧编号 */
    pthread_mutex_t         tran_frm_mutex;
    struct buf              view_frm;           /* frame to preview */

    jpg_enc_t               enc;
    jpg_dec_t               dec;

    fbd_t                   fbd;

    struct wcamsrv          *srv;
};

static void *decJpg2preview(void *arg)
{
    struct vid *v = arg;
    int width, height;
    const void* p;
    int l;

    //pr_debug("jpg framesize = %d\n", v->view_frm.len);
    jpg_dec_frame(v->dec, v->view_frm.start, v->view_frm.len);
    p = jpg_dec_get_outbuf(v->dec, &l);
    jpg_dec_get_frmsiz(v->dec, &width, &height);
    //pr_debug("yuv framesize = %d(%d x %d)\n", l, width, height);

    fbd_show_yuv_frame(v->fbd, p, width, height);
    free(v->view_frm.start);
    v->view_frm.start = NULL;
    return NULL;
}

static void handle_jpeg_img_proc(const void *p, int size, void *arg)
{
    struct vid *v = arg;

    pthread_mutex_lock(&v->tran_frm_mutex);
    v->tran_frm.start = (void*)p;
    v->tran_frm.len   = size;
    v->tran_frm_index++;
    pthread_mutex_unlock(&v->tran_frm_mutex);

    if (v->view_frm.start == NULL) {
        v->view_frm.len = size;
        v->view_frm.start = malloc(size);
        memcpy(v->view_frm.start, p, size);
        pool_add_worker(v->srv->pool, decJpg2preview, v);
    }
}

static void *encJpg4transfer(void *arg)
{
    struct vid *v = arg;
    struct v4l2_frmsizeenum frm;
    void *pbuf;
    int width, height;
    int  l;

    v4l2_get_frmsize(v->cam, v4l2_get_cur_fmt_nr(v->cam),
                             v4l2_get_cur_frm_nr(v->cam), &frm);
    width  = frm.discrete.width; 
    height = frm.discrete.height; 

    fbd_show_yuv_frame(v->fbd, v->view_frm.start, width, height);
    //pr_debug("yuv framesize = %d(%d x %d)\n", v->view_frm.len, width, height);
    jpg_enc_yuyv_frame(v->enc, v->view_frm.start, width, height);
    pbuf = jpg_enc_get_outbuf(v->enc, &l);

    pthread_mutex_lock(&v->tran_frm_mutex);
    if (v->tran_frm_max_size < l) { 
        v->tran_frm.start = realloc(v->tran_frm.start, l);
        v->tran_frm_max_size = l;
    }
    memcpy(v->tran_frm.start, pbuf, l);
    v->tran_frm.len = l;
    v->tran_frm_index++;
    pthread_mutex_unlock(&v->tran_frm_mutex);
    
    //pr_debug("jpg framesize = %d\n", v->tran_frm.len);
    return NULL;
}

static void handle_yuyv_img_proc(const void *p, int size, void *arg)
{
    struct vid *v = arg;

    v->view_frm.len   = size;
    v->view_frm.start = p;
    encJpg4transfer(v);
}

vid_t vid_create(struct wcamsrv *ws) 
{
    struct v4l2_fmtdesc     fmt;
    struct vid *v = calloc(1, sizeof(struct vid));
    if (!v) {
		perror("vid_create");
		return NULL;
	}

    v->srv = ws;
    v->cam = v4l2_create(v->srv->app, cfg_get_camdev(v->srv->cfg), 
                                      cfg_get_cam_fmt_nr(v->srv->cfg),
                                      cfg_get_cam_frm_nr(v->srv->cfg));
    if (v->cam == NULL)
        goto err_mem;
    
	if (pthread_mutex_init(&v->tran_frm_mutex, NULL)) {
		perror("vid_create: pthread_mutex_init");
		goto err_v4l2;	
	}

    v4l2_get_fmt(v->cam, 0, &fmt);

    if (fmt.pixelformat == V4L2_PIX_FMT_JPEG) {
        v4l2_set_img_proc(v->cam, handle_jpeg_img_proc, v);  
        v->dec = jpg_dec_create();
        if (v->dec == NULL)
            goto err_mutex;
    } else if (fmt.pixelformat == V4L2_PIX_FMT_YUYV) {
        v4l2_set_img_proc(v->cam, handle_yuyv_img_proc, v);  
        v->enc = jpg_enc_create();
        if (v->enc == NULL)
            goto err_mutex;
    } else {
        pr_debug("Capture video format is %s, but now we just "
                 "support JPEG and YUYV.\n", 
                 fmt.description); 
        goto err_mutex;
    }

    v->fbd = fbd_create(0, cfg_get_fb_bpp(v->srv->cfg), 0, 0,
                           cfg_get_fb_width(v->srv->cfg),
                           cfg_get_fb_height(v->srv->cfg));
    if (v->fbd == NULL) 
        goto err_codec;

    if (v4l2_start_capture(v->cam))
        goto err_fbd;

    return v;
err_fbd:
    fbd_free(v->fbd);
err_codec: 
    if (v->enc)
        jpg_enc_free(v->enc);
    if (v->dec)
        jpg_dec_free(v->dec);
err_mutex:
    pthread_mutex_destroy(&v->tran_frm_mutex);
err_v4l2:
    v4l2_free(v->cam);
err_mem:
    free(v);
    return NULL;
}

void vid_free(vid_t vid)
{
    struct vid *v = vid;
    v4l2_stop_capture(v->cam);
    fbd_free(v->fbd);
    if (v->enc)
        jpg_enc_free(v->enc);
    if (v->dec)
        jpg_dec_free(v->dec);
    v4l2_free(v->cam);
    free(v);
}

static void vid_get_uctl(struct vid *v, __u8 *req, __u8 *rsp) 
{
    __s32 *val = (__s32*)rsp;
    __u32 *id  = (__u32*)req;
    bool ok;
    *val = v4l2_get_uctl(v->cam, *id, &ok);
}

static __u32 vid_get_uctls(struct vid *v, __u8 *rsp)
{
    struct v4l2_uctl *uctls = (struct v4l2_uctl *)rsp;
	__u32 uctls_nr;
    uctls_nr = v4l2_get_uctls_nr(v->cam);
    v4l2_get_uctls(v->cam, uctls);
    return uctls_nr * sizeof(struct v4l2_uctl);
}

static void vid_set_uctl(struct vid *v, __u8 *req)
{
    __u32 id;  
    __s32 val;  
    memcpy(&id, req, 4);
    memcpy(&val, &req[4], 4);
    v4l2_set_uctl(v->cam, id, val);   
}

#if 0
static __u32 vid_get_fmts(struct vid *v, __u8 *rsp)
{
    __u32 i, j, fmts_nr, frms_nr;
    __u8 *p = rsp;

    fmts_nr = v4l2_get_fmts_nr(v->cam);
    for (i = 0; i < fmts_nr; i++) {
        v4l2_get_fmt(v->cam, i, (struct v4l2_fmtdesc*)p); 
        p += sizeof(struct v4l2_fmtdesc);
        frms_nr = v4l2_get_fmt_frms_nr(v->cam, i);
        memcpy(p, &frms_nr, sizeof(__u32));
        p += sizeof(__u32);
        for (j = 0; j < frms_nr; j++) {
            v4l2_get_frmsize(v->cam, i, j, (struct v4l2_frmsizeenum*)p);
            p += sizeof(struct v4l2_frmsizeenum);
        }
    }
    return (p - rsp);
}

static void vid_set_fmt(struct vid *v, __u8 *req)
{
    __u32 *fmt_nr  = (__u32*)(&req[0]);
    __u32 *frm_nr  = (__u32*)(&req[4]);
    v4l2_set_fmt(v->cam, *fmt_nr, *frm_nr);   
}
#endif

static void vid_get_fmt(struct vid *v, __u8 *rsp) 
{
    __u32 fmt = V4L2_PIX_FMT_JPEG;
    memcpy(rsp, &fmt, 4);
}

static void vid_get_frmsiz(struct vid *v, __u8 *rsp) 
{
    struct v4l2_frmsizeenum frm;
    v4l2_get_frmsize(v->cam, v4l2_get_cur_fmt_nr(v->cam),
                             v4l2_get_cur_frm_nr(v->cam), &frm);
    memcpy(rsp, &frm.discrete, sizeof(struct v4l2_frmsize_discrete));
}

static __u32 vid_get_trans_frame(struct vid *v, __u8 *rsp)
{
    memcpy(rsp, v->tran_frm.start, v->tran_frm.len); 
    return v->tran_frm.len;
}

int vid_cmd_proc(tcpc_t c) 
{
    struct wcamcli  *wc     = c->arg;
    struct vid      *v      = wc->srv->vid;
    __u8            *req    = wc->req;
    __u8            *rsp    = wc->rsp;
    __u8            id      = req[CMD1_POS];
    __u8            status  = ERR_SUCCESS;
    __u8            dat[FRAME_DAT_MAX];
    __u32           pos, len, size;

    switch (id) {
    case REQUEST_ID(VID_GET_UCTL):
        vid_get_uctl(v, &req[DAT_POS], dat);
        build_and_send_rsp(c, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID,
                           id, 4, dat);
		break;
    case REQUEST_ID(VID_GET_UCTLS):
        /*   
         * 应答帧结构: 字节 / 字段名称
         * 1    | 2    | 4                      | 长度由4字节数据部分指定
         * 长度 | 命令 | 数据(控制项列表大小)   | 控制项列表
         */
        len = sizeof(__u32);
        pos = FRAME_HDR_SZ + len;
        size = vid_get_uctls(v, &rsp[pos]);
        build_rsp(rsp, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID, id, len, (__u8*)&size);
        tcpc_send(c, rsp, pos + size);
		break;
    case REQUEST_ID(VID_SET_UCTL):
        vid_set_uctl(v, &req[DAT_POS]);
        break;
    case REQUEST_ID(VID_SET_UCS2DEF):
        v4l2_set_uctls2def(v->cam);
        break;

    case REQUEST_ID(VID_GET_FMT):
        vid_get_fmt(v, dat);
        build_and_send_rsp(c, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID,
                           id, 4, dat);
		break;

    case REQUEST_ID(VID_GET_FRMSIZ):
        vid_get_frmsiz(v, dat);
        build_and_send_rsp(c, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID,
                           id, 8, dat);
		break;

#if 0
    case REQUEST_ID(VID_GET_FMTS):
        /*   
         * 应答帧结构: 字节 / 字段名称
         * 1    | 2    | 4                          | 长度由4字节数据部分指定
         * 长度 | 命令 | 数据(格式分辨率列表大小)   | 格式分辨率列表
         */
        len = sizeof(__u32);
        pos = FRAME_HDR_SZ + len;
        size = vid_get_fmts(v, &rsp[pos]);
        build_rsp(rsp, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID, id, len, (__u8*)&size);
        send_func(c, rsp, pos + size);
        break;
    case REQUEST_ID(VID_SET_FMT):
        vid_set_fmt(v, &req[DAT_POS]);
        break;
#endif

    case REQUEST_ID(VID_REQ_FRAME):
        /*   
         * 应答帧结构: 字节 / 字段名称
         * 1    | 2    | 4                  | 长度由4字节数据部分指定
         * 长度 | 命令 | 数据(图像帧大小)   | 图像帧
         */
        len = sizeof(__u32);
        pos = FRAME_HDR_SZ + len;

        pthread_mutex_lock(&v->tran_frm_mutex);
        if (v->tran_frm_index != wc->last_frm_index) {
            size = vid_get_trans_frame(v, &rsp[pos]);
            wc->last_frm_index = v->tran_frm_index; 
        } else {
            size = 0;
        }
        pthread_mutex_unlock(&v->tran_frm_mutex);

        build_rsp(rsp, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_VID, id, len, (__u8*)&size);
        tcpc_send(c, rsp, pos + size);
        break;

    default:
        status = ERR_CMD_ID;
        break;
    }
	
    return status;
}

#endif

