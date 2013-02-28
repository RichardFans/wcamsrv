#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/epoll.h>

#include <cam/app.h>

#if defined(DBG_APP)
#define pr_debug(fmt, ...) \
    printf("[%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define pr_debug(fmt, ...) \
    do {} while(0)
#endif

struct app_event {
    int fd;
    bool epolled;     /* 1: in epoll wait list, 0 not in */
    uint32_t events;

    void (*handler_rd)(int fd, void *arg);
    void (*handler_wr)(int fd, void *arg);
    void (*handler_er)(int fd, void *arg);
    void *arg_rd;
    void *arg_wr;
    void *arg_er;
};

bool app_event_epolled(app_event_t ev)
{
    struct app_event *e = ev; 
    return e->epolled;
}

app_event_t app_event_create(int fd)
{
    struct app_event *e = calloc(1, sizeof(struct app_event));
    if (!e) {
		perror("app_event_create");
		return NULL;
	}
    e->fd = fd;
    return e;
}

void app_event_add_notifier(app_event_t ev, enum app_notifier_t type, 
                            void (*handler)(int, void *), void *arg)
{
    struct app_event *e = ev; 
    switch (type) {
    case NOTIFIER_READ:
        e->events |= EPOLLIN;
        e->handler_rd = handler;
        e->arg_rd = arg;
        break;
    case NOTIFIER_WRITE:
        e->events |= EPOLLOUT;
        e->handler_wr = handler;
        e->arg_wr = arg;
        break;
    case NOTIFIER_ERROR:
        e->events |= EPOLLERR;
        e->handler_er = handler;
        e->arg_er = arg;
        break;
    }
}

void app_event_free(app_event_t ev)
{
    struct app_event *e = ev; 
    free(e);
}

struct app {
    int epfd;
    int event_max;
    int event_cnt;
    bool need_exit;
};

int app_add_event(app_t app, app_event_t ev)
{
    struct app *a = app; 
    struct app_event *e = ev; 
    struct epoll_event epv;
    int op;

    if (a->event_cnt + 1 > a->event_max) {
        pr_debug("event_cnt is %d now while event_max is %d\n", 
                  a->event_cnt, a->event_max);
        return -1;
    }

    epv.data.ptr = e;
    epv.events = e->events;
    if (ev->epolled) {
        op = EPOLL_CTL_MOD;
    } else {
        op = EPOLL_CTL_ADD;
        ev->epolled = true;
    }

    if(epoll_ctl(a->epfd, op, e->fd, &epv) < 0) {
        pr_debug("Event Add failed: fd = %d\n", e->fd);
        return -1;
    } 

    if (op == EPOLL_CTL_ADD)
        a->event_cnt++;

    pr_debug("Event Add Success: fd = %d, events = %x\n", e->fd, epv.events);
    return 0;
}

int app_del_event(app_t app, app_event_t ev)
{
    struct app *a = app; 
    struct app_event *e = ev; 

    if (e->epolled) {
        e->epolled = false;
        if(epoll_ctl(a->epfd, EPOLL_CTL_DEL, e->fd, NULL) < 0) {
            pr_debug("Event Del failed: fd = %d\n", e->fd);
            return -1;
        } 
        a->event_cnt--;
    }

    pr_debug("Event Del Success: fd = %d, events = %x\n", e->fd, e->events);
    return 0;
}

app_t app_create(int event_max)
{
    struct app *a = calloc(1, sizeof(struct app));
    if (!a) {
		perror("app_create");
		return NULL;
	}

    if (event_max > 0)
        a->event_max = event_max;
    else
        a->event_max = DEF_MAX_EVENT;
    
    a->epfd = epoll_create(a->event_max);
    if (a->epfd == -1) {
        perror("epoll_create");
        goto err_mem;
    }

    return a;
err_mem:
    free(a);
    return NULL;
}

void app_free(app_t app)
{
    struct app *a = app; 
    close(a->epfd);
    free(a);
}

int app_exec(app_t app)
{
    struct app *a = app; 
    struct epoll_event events[a->event_max];
    struct app_event *e;
    uint32_t event;
    int i, fds;
    
    a->need_exit = false;
    while (!a->need_exit) {
        fds = epoll_wait(a->epfd, events, a->event_max, 1000);
        if(fds < 0) {
            perror("epoll_wait");
            return -1;
        }

        for(i = 0; i < fds; i++){
            e = (struct app_event*)events[i].data.ptr;
            event = events[i].events;
            if ((event & EPOLLIN) && (e->events & EPOLLIN)) 
                e->handler_rd(e->fd, e->arg_rd);

            if ((event & EPOLLOUT) && (e->events & EPOLLOUT)) 
                e->handler_wr(e->fd, e->arg_wr);

            if ((event & EPOLLERR) && (e->events & EPOLLERR)) 
                e->handler_er(e->fd, e->arg_er);
        } 
    }
    return 0;
}

void app_finish(app_t app)
{
    struct app *a = app; 
    a->need_exit = true;
}

#if 0
#include <cam/v4l2.h>


void img_proc(const void *p, int size, void *arg)
{
    static int i;
    char buf[32];
    FILE *fp;
    sprintf(buf, "tmp/%d.yuv", ++i);
    fp = fopen(buf, "w");

    fwrite(p, size, 1, fp);
    fclose(fp);
    fprintf(stdout, ".");
    fflush(stdout);
}

int main(int argc, char *argv[])
{
    app_t a = app_create(0);
    
    v4l2_dev_t v = v4l2_create(a, "/dev/video2", 0, 0);
    v4l2_set_img_proc(v, img_proc, v);
    v4l2_start_capture(v);

    app_exec(a);

    v4l2_stop_capture(v);
    v4l2_free(v);

    app_free(a);

    return 0;
}
#endif

