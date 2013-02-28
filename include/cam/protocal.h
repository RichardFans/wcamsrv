/**
 * 本协议参考了CC2530-ZNP接口规范
 * 
 * 通用帧格式如下.
 * 字节:
 * 1    | 2    | 0-250 
 * 长度 | 命令 | 数据  
 * 
 * 长度: 帧的数据字段长度. 
 * 命令: 帧命令.
 * 数据: 帧数据.
 * 
 * 命令字段由cmd0和cmd1两字节构成, 格式如下: 
 * Cmd0 | Cmd1
 * Bits:
 * 7-5  | 4-0    | 0-7
 * 类型 | 子系统 | ID 
 * 
 * 类型: 命令类型有如下几种值:
 * 1: 同步请求: A synchronous request that requires an immediate response. For
 * example, a function call with a return value would use an SREQ command. 
 * 2: 异步请求: An asynchronous request. For example, a callback event or a
 * function call with no return value would use an AREQ command. 
 * 3: 同步应答: A synchronous response. This type of command is only sent in
 * response to a SREQ command.
 * 
 * 子系统: 命令子系统. 值如下：
 * 0 远程通信错误接口
 * 1 系统接口
 * 3 视频采集接口
 * 2, 4-32保留
 */
#ifndef __PROTOCAL_H__
#define __PROTOCAL_H__

    #define FRAME_MAX_SZ    253
    #define FRAME_DAT_MAX   253
    #define FRAME_HDR_SZ    3

    #define FRAME_ERR_SZ    3

    #define TYPE_MASK       0xE0
    #define TYPE_BIT_POS    5
    #define SUBS_MASK       0x1F

    #define LEN_POS         0
    #define CMD0_POS        1
    #define CMD1_POS        2
    #define DAT_POS         3

    /* Error codes */
    #define ERR_SUCCESS     0   /* success */
    #define ERR_SUBS        1   /* invalid subsystem */
    #define ERR_CMD_ID      2   /* invalid command ID */
    #define ERR_PARAM       3   /* invalid parameter */
    #define ERR_LEN         4   /* invalid length */

    #define TYPE_SREQ    	0x1
	#define TYPE_AREQ     	0x2
	#define TYPE_SRSP     	0x3

	/**
	 * 远程通信错误接口
	 */
	#define SUBS_ERR  		0x0
	/**
	 * 系统接口
	 */
	#define SUBS_SYS  		0x1

	/**
	 * 视频采集接口
	 */
	#define SUBS_VID  		0x3

	#define SUBS_MAX  		0x4
	
	/**
	 * 构造请求的宏
	 */
	#define REQUEST(len, type, subs, id)	(((len) << (8*LEN_POS)) | \
											 (((type) << TYPE_BIT_POS | (subs)) << (8*CMD0_POS)) | \
											 ((id) << (8*CMD1_POS)))

#endif //__PROTOCAL_H__

