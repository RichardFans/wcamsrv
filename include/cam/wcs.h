#ifndef __WCAMSRV_H__
#define __WCAMSRV_H__

#define DEF_SRV_PORT        19868

typedef struct wcamsrv      *wcs_t;

wcs_t wcs_create(char *cfg_path);
int wcs_run(wcs_t wcs);
void wcs_free(wcs_t wcs);

#endif
