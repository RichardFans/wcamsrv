#ifndef __JPEG_H__
#define __JPEG_H__

#define JPG_BUF_MAX_SIZE            0x200000        /* 2MB */

typedef struct jpg_enc  *jpg_enc_t;

jpg_enc_t jpg_enc_create();
void jpg_enc_free(jpg_enc_t enc);
int jpg_enc_yuyv_frame(jpg_enc_t enc, const void *frm, int w, int h);
void *jpg_enc_get_outbuf(jpg_enc_t enc, int *len);


typedef struct jpg_dec  *jpg_dec_t;

jpg_dec_t jpg_dec_create();
void jpg_dec_free(jpg_dec_t dec);
int jpg_dec_frame(jpg_dec_t dec, const void *jpg_frm, int len);
void *jpg_dec_get_outbuf(jpg_dec_t dec, int *len);
void jpg_dec_get_frmsiz(jpg_dec_t dec, int *w, int *h);

#endif
