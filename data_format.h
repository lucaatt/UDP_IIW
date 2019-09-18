#ifndef UDP_IIW_DATA_FORMAT_H
#define UDP_IIW_DATA_FORMAT_H

#define DATA_SIZE 1484
#define N 2
#define MAX_SEQ_NUM 60000
#define READY_SIZE 5
//window size

typedef struct packet{
    //int cmd;
    unsigned int seq_num;
    unsigned int ack_num;
    short ack;
    short last;
    char data[DATA_SIZE];//AGGIUNGERE 2BYTE DI LAST
}packet;

typedef struct ctrl_packet{
    int cmd;
    unsigned int seq_num;
    unsigned int ack_num;
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
}ack_thread_args;

typedef struct send_thread_args{
    struct window *wnd;
    struct packet ready[READY_SIZE];
    short slots[READY_SIZE];
    struct sockaddr_in servaddr;
    int sockfd;
}send_thread_args;

#endif
