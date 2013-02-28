#ifndef __REQUEST_H__
#define __REQUEST_H__
	
#include <cam/protocal.h>
#include <cam/v4l2.h>
enum request {

	SYS_VERSION		=	REQUEST(0x0, TYPE_SREQ, SUBS_SYS, 0x0),

	/**
	 * VID SubSystem
	 */
	VID_GET_UCTLS	=	REQUEST(0x0, TYPE_SREQ, SUBS_VID, 0x0), 
	VID_GET_UCTL	=	REQUEST(0x4, TYPE_SREQ, SUBS_VID, 0x1), 
	VID_SET_UCTL	=	REQUEST(0x8, TYPE_AREQ, SUBS_VID, 0x2), 
	VID_SET_UCS2DEF	=	REQUEST(0x0, TYPE_AREQ, SUBS_VID, 0x3), 

	VID_GET_FRMSIZ	=	REQUEST(0x0, TYPE_SREQ, SUBS_VID, 0x10), 
	VID_GET_FMT	    =	REQUEST(0x0, TYPE_SREQ, SUBS_VID, 0x11), 

	VID_REQ_FRAME	=	REQUEST(0x0, TYPE_SREQ, SUBS_VID, 0x20),
};

#define REQUEST_ID(req)     (((req) >> (8*CMD1_POS)) & 0xFF)
#define REQUEST_TYPE(req)   (((req) >> (8*CMD0_POS + TYPE_BIT_POS)) & TYPE_MASK)
#define REQUEST_SUBS(req)   (((req) >> (8*CMD0_POS)) & SUBS_MASK)
#define REQUEST_LEN(req)    (((req) >> (8*LEN_POS)) & 0xFF)

#endif //__REQUEST_H__

