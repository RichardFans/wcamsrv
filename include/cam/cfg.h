#ifndef __CFG_H__
#define __CFG_H__

typedef struct cfg *cfg_t;

cfg_t cfg_create(char *cfg_path);
void cfg_free(cfg_t cfg);

char *cfg_get_version(cfg_t cfg);

int cfg_get_srvport(cfg_t cfg);
int cfg_get_cli_timeout(cfg_t cfg);
int cfg_get_cli_timeout_check(cfg_t cfg);
int cfg_get_max_app_event(cfg_t cfg);

char *cfg_get_camdev(cfg_t cfg);
int cfg_get_cam_fmt_nr(cfg_t cfg);
int cfg_get_cam_frm_nr(cfg_t cfg);

int cfg_get_fb_bpp(cfg_t cfg);
int cfg_get_fb_width(cfg_t cfg);
int cfg_get_fb_height(cfg_t cfg);

int cfg_get_thread_in_pool(cfg_t cfg);

#define MAX_LINE_LEN 	256
#define DEF_CFG_PATH 	"/root/wcamsrv/config"
#define DEF_VERSION 	"GQ Webcam version 2.0"

#endif

