#ifndef __WCAM_SRV_H__
#define __WCAM_SRV_H__

#include <sys/types.h>

#include <cam/wcs.h>

#include <cam/request.h>

#include <cam/threadpool.h>
#include <cam/tcp_srv.h>
#include <cam/utils.h>
#include <cam/cfg.h>
#include <cam/v4l2.h>
#include <cam/jpg.h>
#include <cam/fbd.h>

#if defined(VID_FUNC)
#define VID_FRAME_MAX_SZ    (0xFFFFF - FRAME_MAX_SZ)
typedef struct vid *vid_t;

vid_t vid_create(struct wcamsrv *ws);
void vid_free(vid_t vid);

int vid_cmd_proc(tcpc_t c);
#endif

struct wcamsrv {
    app_t                   app;
    thread_pool_t           pool;

#if defined(VID_FUNC)
    vid_t                   vid;
#endif

    tcp_srv_t               srv;
    cfg_t                   cfg;
};

struct wcamcli {
    int         req_total;
    int         req_cnt;
    __u8        req[FRAME_MAX_SZ];
#if defined(VID_FUNC)
    __u8        rsp[FRAME_MAX_SZ + VID_FRAME_MAX_SZ];
    __u64       last_frm_index;
#else
    __u8        rsp[FRAME_MAX_SZ];
#endif
   
    wcs_t       srv;
};

void build_and_send_rsp(tcpc_t c, __u8 type, __u8 id, 
                        __u8 len, __u8 *data);

int build_rsp(__u8 *rsp, __u8 type, __u8 id, 
                        __u8 len, __u8 *data);

#endif

