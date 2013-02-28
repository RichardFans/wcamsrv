#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include <cam/list.h>
#include <cam/utils.h>
#include <cam/tcp_srv.h>

#if defined(DBG_TCP)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct tcp_cli {
    int                     sock;           /* 客户端套结字 */
    struct sockaddr_in      addr;           /* 客户端网络地址 */
    void                    *arg;           /* 客户端私有数据初始化函数 */

    app_event_t             ev_rx;          /* 客户端读事件 */
    app_event_t             ev_tx;          /* 客户端写事件 */

    char                    *buf;           /* buf to send */
    int                     len;            /* buf length */

    time_t                  last_active;    /* 用于超时处理 */    
	struct list_head        entry;     

    struct tcp_srv          *srv;           /* 对应服务器 */
};

struct tcp_srv {
    int                     sock;           /* 服务器监听套结字 */
	struct list_head        cli_list;       /* 客户端列表 */

	pthread_t               tid;            /* 用于检查超时的线程 */      
    pthread_mutex_t         mutex;
    bool                    need_stop;
    int                     timeout_check_per_s;  
    int                     timeout;       

    app_t                   app;
    app_event_t             ev;             /* 监听事件 */

    void                    *arg;           /* 客户端初始化函数传入参数 */
    int  (*init)(tcpc_t, void*);            /* 客户端私有数据初始化函数 */
    void (*uninit)(tcpc_t);                 /* 客户端私有数据反初始化函数 */

    tcpc_handler_t          recv_handler;
};

void tcpc_send(tcpc_t tc, void *buf, int len)
{
	struct tcp_cli *c = (struct tcp_cli*)tc;
	struct tcp_srv *s = c->srv;
     
    app_del_event(s->app, c->ev_rx);
    c->buf = buf;
    c->len = len;
    app_add_event(s->app, c->ev_tx);
}

static void tcpc_free(struct tcp_cli *c)
{
	struct tcp_srv *s = c->srv;
    if (app_event_epolled(c->ev_rx))
        app_del_event(s->app, c->ev_rx);
    else if (app_event_epolled(c->ev_tx))
        app_del_event(s->app, c->ev_tx);

    if (s->uninit)  
        s->uninit((tcpc_t)c);

    app_event_free(c->ev_rx);
    app_event_free(c->ev_tx);
    close(c->sock);
    free(c);   
}

static void rx_app_handler(int sock, void *arg)
{
	struct tcp_cli *c = arg;
	struct tcp_srv *s = c->srv;
    char buf[BUFSIZ];
    int res = 0;

    if (sock != c->sock) {
        pr_debug("sock = %d, c->sock = %d.\n", sock, c->sock);
        return;
    } 

    c->last_active = time(NULL);
    if (s->recv_handler) {
        res = s->recv_handler((tcpc_t)c);
    } else {
        res = recv(sock, buf, BUFSIZ, 0);
    }

    if (res <= 0) { /* connection closed */
        if (res < 0) {
            perror("rx_app_handler");
        } else {
            pr_debug("client(addr: %s, port: %d, sock: %d) has disconnected\n", 
                    inet_ntoa(c->addr.sin_addr), c->addr.sin_port, c->sock);
        }
        pthread_mutex_lock(&s->mutex);
        list_del(&c->entry);
        pthread_mutex_unlock(&s->mutex);
        tcpc_free(c); 
    }
}

static void tx_app_handler(int sock, void *arg)
{
	struct tcp_cli *c = arg;
	struct tcp_srv *s = c->srv;
    int res = 0;

    if (sock != c->sock) {
        pr_debug("sock = %d, c->sock = %d.\n", sock, c->sock);
        return;
    } 

    c->last_active = time(NULL);
    res = send(sock, c->buf, c->len, 0);

    if (res > 0) {
        c->len -= res;
        if (c->len == 0) {
            app_del_event(s->app, c->ev_tx);
            app_add_event(s->app, c->ev_rx);
        }
    } else {
        if (res < 0) {
            perror("tx_app_handler");
        } else {
            pr_debug("client(addr: %s, port: %d, sock: %d) has disconnected\n", 
                    inet_ntoa(c->addr.sin_addr), c->addr.sin_port, c->sock);
        }
        pthread_mutex_lock(&s->mutex);
        list_del(&c->entry);
        pthread_mutex_unlock(&s->mutex);
        tcpc_free(c); 
    }
}

static void srv_app_handler(int sock, void *arg)
{
	struct tcp_srv *s = arg;
    struct tcp_cli *c;
    int nfd;
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);

    if (sock != s->sock) {
        pr_debug("sock = %d, s->sock = %d.\n", sock, s->sock);
        return;
    } 

    if((nfd = accept(s->sock, (struct sockaddr*)&sin, &len)) == -1) {
        if(errno != EAGAIN && errno != EINTR)
            perror("accept");
        return;
    }

    if (-1 == setnonblocking(nfd))
        goto err_rej;

    c = calloc(1, sizeof(struct tcp_cli));
    if (!c) {
        perror("calloc tcp_cli");
		goto err_rej;
	}  
    c->sock = nfd;
    memcpy(&c->addr, &sin, len);
    c->srv = s;
    pr_debug("client(addr: %s, port: %d, sock: %d) has connected\n", 
            inet_ntoa(c->addr.sin_addr), c->addr.sin_port, c->sock);

    c->last_active = time(NULL);

    c->ev_rx = app_event_create(c->sock);
    if (NULL == c->ev_rx) 
        goto err_mem;
    app_event_add_notifier(c->ev_rx, NOTIFIER_READ, rx_app_handler, c);

    c->ev_tx = app_event_create(c->sock);
    if (NULL == c->ev_tx) 
        goto err_evrx;
    app_event_add_notifier(c->ev_tx, NOTIFIER_WRITE, tx_app_handler, c);

    if (s->init && s->init((tcpc_t)c, s->arg) == -1)
        goto err_evtx; 

    if (app_add_event(c->srv->app, c->ev_rx) == -1)
        goto err_init;

    pthread_mutex_lock(&s->mutex);
    list_add_tail(&c->entry, &s->cli_list); 
    pthread_mutex_unlock(&s->mutex);


    return;
err_init:
    if (s->uninit)  
        s->uninit((tcpc_t)c);
err_evtx:
    app_event_free(c->ev_tx);
err_evrx:
    app_event_free(c->ev_rx);
err_mem:
    free(c);
err_rej:
    close(nfd);

    pr_debug("client(addr: %s, port: %d, sock: %d) has disconnected\n", 
            inet_ntoa(c->addr.sin_addr), c->addr.sin_port, c->sock);
}

static void *timeout_handler(void *arg)
{
    struct tcp_srv *s = arg; 
    struct tcp_cli *c, *tmpc; 
    time_t now, duration; 
    
	while (!s->need_stop) {
        sleep(s->timeout_check_per_s);
        if (list_empty(&s->cli_list))
            continue;

        now = time(NULL);
        pthread_mutex_lock(&s->mutex);
        list_for_each_entry_safe(c, tmpc, &s->cli_list, entry) {  
            duration = now - c->last_active; 
            if (duration >= s->timeout) {
                pr_debug("duration = %lu, s->timeout = %d\n", duration, s->timeout);
                pr_debug("client(addr: %s, port: %d, sock: %d) timeout\n", 
                         inet_ntoa(c->addr.sin_addr), c->addr.sin_port, c->sock);
                list_del(&c->entry);
                tcpc_free(c);
            }
        }
        pthread_mutex_unlock(&s->mutex);
    }
    return NULL;
}

void tcps_set_cli_recvhandler(tcp_srv_t srv, tcpc_handler_t handler)
{
    struct tcp_srv *s = srv;
    s->recv_handler = handler;
}

void tcps_set_cli_init(tcp_srv_t srv, int (*init)(tcpc_t, void*), void *arg)
{
    struct tcp_srv *s = srv;
    s->init = init;
    s->arg  = arg;
}

void tcps_set_cli_uninit(tcp_srv_t srv, void (*uninit)(tcpc_t))
{
    struct tcp_srv *s = srv;
    s->uninit = uninit;
}

void tcps_set_timeout(tcp_srv_t srv, int timeout)
{
    struct tcp_srv *s = srv;
    s->timeout = timeout; 
}

void tcps_set_timeout_check(tcp_srv_t srv, int per_s)
{
    struct tcp_srv *s = srv;
    s->timeout_check_per_s = per_s; 
}

tcp_srv_t tcps_create(app_t app, int port) 
{
    struct tcp_srv *s = calloc(1, sizeof(struct tcp_srv));
    if (!s) {
		perror("tcps_create");
		return NULL;
	}

	if (port < 1000)
		port = DEF_TCP_SRV_PORT;

    if (-1 == (s->sock = tcp_srv_sock(port))) 
        goto err_mem;

    if (-1 == setnonblocking(s->sock))
        goto err_mem;

	INIT_LIST_HEAD(&s->cli_list);
    s->timeout_check_per_s = DEF_TIMEOUT_CHECK_PER_S;
    s->timeout = DEF_TIMEOUT;
	if (pthread_mutex_init(&s->mutex, NULL)) {
		perror("tcps_create: pthread_mutex_init");
		goto err_sock;	
	}
    
	if (pthread_create(&s->tid, NULL, timeout_handler, s)) {
		perror("tcps_create: pthread_create");
		goto err_mutex;
	}
    
    s->ev = app_event_create(s->sock);
    if (NULL == s->ev) 
        goto err_thread;
    app_event_add_notifier(s->ev, NOTIFIER_READ, srv_app_handler, s);

    s->app = app;
    if (app_add_event(s->app, s->ev) == -1) 
        goto err_appev;

	return s;
err_appev:
    app_event_free(s->ev);
err_thread:
    s->need_stop = true;
    pthread_join(s->tid, NULL);
err_mutex:
    pthread_mutex_destroy(&s->mutex);
err_sock:
    close(s->sock);
err_mem:
    free(s);
    return NULL;
}

void tcps_free(tcp_srv_t srv)
{
    struct tcp_srv *s = srv;
    struct tcp_cli *c, *tmpc; 

    app_del_event(s->app, s->ev);
    app_event_free(s->ev);
    s->need_stop = true;
    pthread_join(s->tid, NULL);
    pthread_mutex_destroy(&s->mutex);
    list_for_each_entry_safe(c, tmpc, &s->cli_list, entry) {  
        list_del(&c->entry);
        tcpc_free(c);
    }
    close(s->sock);
    free(s);
}

