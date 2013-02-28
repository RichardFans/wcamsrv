#ifndef	__APP_H__
	#define __APP_H__

typedef struct app_event *app_event_t;
typedef struct app *app_t;

enum app_notifier_t {
    NOTIFIER_READ  = 0,
    NOTIFIER_WRITE = 1,
    NOTIFIER_ERROR = 2,
};

#define DEF_MAX_EVENT 	512

app_event_t app_event_create(int fd);
void app_event_add_notifier(app_event_t ev, 
                            enum app_notifier_t type, 
                            void (*handler)(int, void *), 
                            void *arg);
void app_event_free(app_event_t ev);

app_t app_create(int event_max);
void app_free(app_t app);
int app_exec(app_t app);
int app_add_event(app_t app, app_event_t ev);
int app_del_event(app_t app, app_event_t ev);

bool app_event_epolled(app_event_t ev);

#endif	//__APP_H__
