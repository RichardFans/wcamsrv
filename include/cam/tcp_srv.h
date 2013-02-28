#ifndef __TCP_SRV_H__
#define __TCP_SRV_H__

#include <cam/app.h>

#define DEF_TCP_SRV_PORT        19868
#define DEF_TIMEOUT             60
#define DEF_TIMEOUT_CHECK_PER_S 1

#include <netinet/in.h>

typedef struct tcp_srv *tcp_srv_t;

struct tcpclient {
    int                     sock;           /* 客户端套结字 */
    struct sockaddr_in      addr;           /* 客户端网络地址 */
    void                    *arg;           /* 客户端私有数据 */
};

typedef struct tcpclient *tcpc_t;
typedef int (*tcpc_handler_t)(tcpc_t);

void tcps_set_cli_recvhandler(tcp_srv_t srv, tcpc_handler_t handler);
void tcps_set_cli_init(tcp_srv_t srv, int (*init)(tcpc_t, void *), void *arg);
void tcps_set_cli_uninit(tcp_srv_t srv, void (*uninit)(tcpc_t));
void tcps_set_timeout(tcp_srv_t srv, int timeout);
void tcps_set_timeout_check(tcp_srv_t srv, int per_s);

tcp_srv_t tcps_create(app_t app, int port);
void tcps_free(tcp_srv_t srv);
void tcpc_send(tcpc_t tc, void *buf, int len);

#endif

