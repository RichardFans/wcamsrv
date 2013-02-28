#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <cam/utils.h>
#include <cam/tcp_srv.h>
#include <cam/threadpool.h>
#include <cam/v4l2.h>
#include <cam/wcs.h>
#include <cam/app.h>
#include <cam/fbd.h>
#include <cam/cfg.h>

#if defined(DBG_CFG)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct cfg {
    char *path;

    /* version */
	char *version;

    /* tcp srv  */
    int srv_port;
    int cli_timeout;
    int cli_timeout_check;

    /* app */
    int max_app_event;

    /* v4l2 */
    char *camdev;
    int cam_fmt_nr;
    int cam_frm_nr;

    /* fb display */
    int fb_bpp;
    int fb_width;
    int fb_height;

    int thread_in_pool;
};

static char cfg_def_version[MAX_LINE_LEN] = {DEF_VERSION};
static char cfg_def_camdev[MAX_LINE_LEN] = {DEF_V4L_DEV};

static struct cfg def_cfg = {
	.version = cfg_def_version,
	.srv_port = DEF_SRV_PORT,
	.cli_timeout = DEF_TIMEOUT,
	.cli_timeout_check = DEF_TIMEOUT_CHECK_PER_S,
	.max_app_event = DEF_MAX_EVENT,
	.camdev = cfg_def_camdev,
    .fb_bpp = DEF_FB_BPP,
    .fb_width = DEF_FB_WIDTH,
    .fb_height = DEF_FB_HEIGHT,
    .thread_in_pool = DEF_THREAD_IN_POOL,
    .cam_fmt_nr = 0,
    .cam_frm_nr = 0,
	//...
};

static int parse_cfg(cfg_t jcfg) 
{    
    struct cfg *c = jcfg;
    char line[MAX_LINE_LEN], arg[MAX_LINE_LEN], val[MAX_LINE_LEN];
    char *pst;
    FILE *fp;

    if ((fp = fopen(c->path, "r"))==NULL) {
        perror("parse_cfg: fopen");
        return -1;
    }

    while (fgets(line, MAX_LINE_LEN, fp) != NULL) {
        if ((pst = strnchr(line, ' ')) == NULL || pst[0] == '#' || pst[0] == '\n')
            continue;

        pst = strtok(pst, " =");
        strcpy(arg, pst);

        pst = strtok(NULL, "\n");
        pst = strchr(pst, '=');
        pst++;
        if ((pst = strnchr(pst, ' ')) == NULL || pst[0] == '#' || pst[0] == '\n')
            continue;

        strcpy(val, pst);
        if (val[strlen(val)-1] == '\n')
            val[strlen(val)-1] = 0;
        
        if (!strcmp(arg, "version")) {
            strcpy(c->version, val); 
        } else if(!(strcmp(arg, "srv_port"))) {
            c->srv_port = atoi(val); 
        } else if(!(strcmp(arg, "cli_timeout"))) {
            c->cli_timeout = atoi(val); 
        } else if(!(strcmp(arg, "cli_timeout_check"))) {
            c->cli_timeout_check = atoi(val); 
        } else if(!(strcmp(arg, "max_app_event"))) {
            c->max_app_event = atoi(val); 
        } else if(!(strcmp(arg, "camdev"))) {
            strcpy(c->camdev, val); 
        } else if(!(strcmp(arg, "fb_bpp"))) {
            c->fb_bpp = atoi(val); 
        } else if(!(strcmp(arg, "fb_width"))) {
            c->fb_width = atoi(val); 
        } else if(!(strcmp(arg, "fb_height"))) {
            c->fb_height = atoi(val); 
        } else if(!(strcmp(arg, "thread_in_pool"))) {
            c->thread_in_pool = atoi(val); 
        } else if(!(strcmp(arg, "cam_fmt_nr"))) {
            c->cam_fmt_nr = atoi(val); 
        } else if(!(strcmp(arg, "cam_frm_nr"))) {
            c->cam_frm_nr = atoi(val); 
        }
    }
#if defined(DBG_CFG)
    pr_debug("configuration:\n"
             "version = %s\n"
             "srv_port = %d\n"
             "cli_timeout = %d\n"
             "cli_timeout_check = %d\n"
             "max_app_event = %d\n"
             "camdev = %s\n"
             "fb_bpp = %d\n"
             "fb_width = %d\n"
             "fb_height = %d\n"
             "thread_in_pool = %d\n"
             "cam_fmt_nr = %d\n"
             "cam_frm_nr = %d\n",
             c->version,
             c->srv_port,
             c->cli_timeout,
             c->cli_timeout_check,
             c->max_app_event,
             c->camdev,
             c->fb_bpp,
             c->fb_width,
             c->fb_height,
             c->thread_in_pool,
             c->cam_fmt_nr,
             c->cam_frm_nr);
#endif
    return 0;
}

cfg_t cfg_create(char *cfg_path) 
{
    struct cfg *c = calloc(1, sizeof(struct cfg));
    if (!c) {
		perror("cfg");
		return NULL;
	}
	
    memcpy(c, &def_cfg, sizeof(struct cfg));

	c->path = cfg_path;
    if (c->path != NULL && parse_cfg(c) != 0) {
		fprintf(stderr, "cfg_create: parse_cfg error, use default.");
		memcpy(c, &def_cfg, sizeof(struct cfg));
	}

	return c;
}

void cfg_free(cfg_t cfg)
{
    struct cfg *c = cfg;
	free(c);
}

int cfg_get_srvport(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->srv_port;
}

int cfg_get_cli_timeout(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->cli_timeout;
}

int cfg_get_cli_timeout_check(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->cli_timeout_check;
}

int cfg_get_max_app_event(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->max_app_event;
}

char *cfg_get_camdev(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->camdev;
}

int cfg_get_cam_fmt_nr(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->cam_fmt_nr;
}

int cfg_get_cam_frm_nr(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->cam_frm_nr;
}

int cfg_get_fb_bpp(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->fb_bpp;
}

int cfg_get_fb_width(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->fb_width;
}

int cfg_get_fb_height(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->fb_height;
}

int cfg_get_thread_in_pool(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->thread_in_pool;
}

char *cfg_get_version(cfg_t cfg)
{
    struct cfg *c = cfg;
	return c->version;
}

#if 0
int main(int argc, char *argv[])
{
    cfg_create(DEF_CFG_PATH);
    return 0;
}
#endif

