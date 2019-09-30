#include <time.h>
#include <pthread.h>
#ifndef UDP_IIW_DATA_FORMAT_H
#define UDP_IIW_DATA_FORMAT_H

#define DATA_SIZE 1500
#define N 80
#define MAX_SEQ_NUM 60000

typedef struct packet{
    //int cmd;
    unsigned int seq_num;
    unsigned int ack_num;
    short TO;
    short ack;
    short fin;
    short last;
    char data[DATA_SIZE];//AGGIUNGERE 2BYTE DI LAST
}packet;

typedef struct ctrl_packet{
    int cmd;
    unsigned int seq_num;
    unsigned int ack_num;
    short TO;
    short ack;
    short fin;
    short syn;
}ctrl_packet;

typedef struct window{
    unsigned int inf;
    unsigned int sup;
    short acked[N];
    struct packet wnd_buff[N];
}window;

typedef struct ack_thread_args{
    struct window *wnd;
    int sockfd;
    struct sockaddr_in *servaddr;
    timer_t *timers;
    struct timespec *send_time;
    int *to_seq_num;
    struct itimerspec *timeout;
    pthread_mutex_t *inf_mux;
    pthread_cond_t *inf_cv;
    pthread_mutex_t *to_mutexes;
    pthread_rwlock_t *to_rwlock;
    pthread_mutex_t *locks;
}ack_thread_args;

typedef struct rtx_thread_args{
    int timer_num;
    struct ack_thread_args *shared;
}rtx_thread_args;

typedef struct TO_thread_args{
    struct ack_thread_args *ack_args;
    struct timespec *ack_time;
    struct timespec *estim_rtt;
    struct timespec *dev_rtt;
}TO_thread_args;

#endif
