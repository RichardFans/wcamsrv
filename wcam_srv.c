#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>

#include "wcam_priv.h"

#if defined(DBG_WCAM)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

/*
 * Process Incoming Request
 */
static tcpc_handler_t process_incomings[] = {
    NULL,   /* 0x0 */

            /* 0x1 */
#if defined(SYS_FUNC)
    sys_cmd_proc,
#else
    NULL,
#endif

            /* 0x2 */
    NULL,

            /* 0x3 */
#if defined (VID_FUNC)
    vid_cmd_proc,
#else
    NULL,
#endif
};

#if defined(DBG_WCAM)
static void print_frame(__u8 *req)
{
    int i;
    int len = req[LEN_POS];
    pr_debug("print_frame:\n");
    pr_debug("len : %02x\n", len);
    pr_debug("cmd0: %02x\n", req[CMD0_POS]);
    pr_debug("cmd1: %02x\n", req[CMD1_POS]);
    pr_debug("dat : ");
    for (i = 0; i < len; i++) 
        printf("%02x ", req[DAT_POS + i]); 
    printf("\n");
}
#else
#define print_frame(req) do {} while(0)
#endif

int build_rsp(__u8 *rsp, __u8 type, __u8 id, 
                         __u8 len, __u8 *data)
{
    rsp[LEN_POS]  = len;
    rsp[CMD0_POS] = type;
    rsp[CMD1_POS] = id;
    memcpy(&rsp[DAT_POS], data, len);
    print_frame(rsp);
    return len + FRAME_HDR_SZ;
}

void build_and_send_rsp(tcpc_t c, __u8 type, __u8 id, 
                        __u8 len, __u8 *data)
{
    struct wcamcli  *wc   = c->arg;
    __u8  *rsp = wc->rsp;

    rsp[LEN_POS]  = len;
    rsp[CMD0_POS] = type;
    rsp[CMD1_POS] = id;
    memcpy(&rsp[DAT_POS], data, len);
    print_frame(rsp);
    tcpc_send(c, rsp, len + FRAME_HDR_SZ);
}

static void process_incoming(tcpc_t c) 
{
    struct wcamcli  *wc   = c->arg;
    tcpc_handler_t func;
    __u8 *req = wc->req;
    __u8  rsp[FRAME_ERR_SZ];

    rsp[1] = req[CMD0_POS];
    rsp[2] = req[CMD1_POS];

    print_frame(req);

    if (req[LEN_POS] > FRAME_DAT_MAX) {
        rsp[0] = ERR_LEN;
    } else if ((rsp[1] & SUBS_MASK) < SUBS_MAX) {
        func = process_incomings[rsp[1] & SUBS_MASK];
        if (func)
            rsp[0] = (*func)(c);
        else
            rsp[0] = ERR_SUBS;
    } else {
        rsp[0] = ERR_SUBS;
    }
    
    if ((rsp[0] != ERR_SUCCESS) && ((rsp[1] & TYPE_MASK) == TYPE_SREQ)) {
        build_and_send_rsp(c, (TYPE_SRSP << TYPE_BIT_POS) | SUBS_ERR, 
                           0, FRAME_ERR_SZ, rsp);
    }
}

static int cli_handler(tcpc_t c) 
{
    struct wcamcli  *wc   = c->arg;
    int             res;
    __u8            *pbuf;
    
    pbuf  = &wc->req[wc->req_cnt]; 

    res = read(c->sock, pbuf, wc->req_total - wc->req_cnt);
    if (res > 0) {
        wc->req_cnt += res;
        if (wc->req_cnt == FRAME_HDR_SZ) {
            wc->req_total += wc->req[LEN_POS];
        } 
        if (wc->req_cnt == wc->req_total) {
            process_incoming(c);
            wc->req_total = FRAME_HDR_SZ;
            wc->req_cnt   = 0;
        }
    }
    return res;
}

static int cli_init(tcpc_t c, void *arg)
{
    struct wcamcli *wc = calloc(1, sizeof(struct wcamcli));
    if (!wc) {
		perror("cli_init");
		return -1;
	}
    pr_debug("wc->srv = %p\n", arg);
    wc->srv = arg;
    wc->req_total = FRAME_HDR_SZ;
    c->arg = wc;
    return 0;
}

static void cli_uninit(tcpc_t c)
{
    struct wcamcli *wc = c->arg;
    free(wc);
}

static int wcs_srv_init(struct wcamsrv* ws)
{
    ws->srv = tcps_create(ws->app, cfg_get_srvport(ws->cfg));
    if (ws->srv == NULL)
        return -1;

    if (cfg_get_cli_timeout(ws->cfg) > 0)
        tcps_set_timeout(ws->srv, cfg_get_cli_timeout(ws->cfg));

    if (cfg_get_cli_timeout_check(ws->cfg) > 0)
        tcps_set_timeout_check(ws->srv, cfg_get_cli_timeout_check(ws->cfg));

    tcps_set_cli_init(ws->srv, cli_init, ws);
    tcps_set_cli_uninit(ws->srv, cli_uninit);
    tcps_set_cli_recvhandler(ws->srv, cli_handler);
    return 0;
}

static inline void wcs_srv_free(struct wcamsrv* ws) {
    tcps_free(ws->srv);
}

wcs_t wcs_create(char *cfg_path) 
{
    struct wcamsrv *ws = calloc(1, sizeof(struct wcamsrv));
    if (!ws) {
		perror("wcs_create");
		return NULL;
	}

    ws->cfg = cfg_create(cfg_path);
    if (ws->cfg == NULL)
        goto err_mem;

    ws->app = app_create(cfg_get_max_app_event(ws->cfg));
    if (ws->app == NULL)
        goto err_cfg;

    ws->pool = pool_create(cfg_get_thread_in_pool(ws->cfg));
    if (ws->pool == NULL)
        goto err_app;

#if defined(VID_FUNC)
    ws->vid = vid_create(ws);
    if (ws->vid == NULL)
        goto err_pool;
#endif

    if (wcs_srv_init(ws) == -1)
        goto err_cam;

	return ws;
err_cam:
#if defined(VID_FUNC)
    vid_free(ws->vid);
#endif
err_pool:
    pool_free(ws->pool);
err_app:
    app_free(ws->app);
err_cfg:
    cfg_free(ws->cfg);
err_mem:
    free(ws);
    return NULL;
}

void wcs_free(wcs_t wcs)
{
    struct wcamsrv *ws = wcs;
    wcs_srv_free(ws);
#if defined(VID_FUNC)
    vid_free(ws->vid);
#endif
    pool_free(ws->pool);
    app_free(ws->app);
    cfg_free(ws->cfg);
    free(ws);
}

int wcs_run(wcs_t wcs) 
{
    struct wcamsrv *ws = wcs;
    return app_exec(ws->app);
}

#if 1
int main(int argc, char *argv[])
{
    wcs_t ws = wcs_create(DEF_CFG_PATH);
    if (ws == NULL)
        return -1;
    return wcs_run(ws);
}
#endif

