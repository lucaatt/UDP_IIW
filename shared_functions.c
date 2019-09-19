#include "imports.h"
#include "data_format.h"

void err_handler(char *who, char *what){
    printf("\n%s %s error\n", who, what);
    perror(what);
    exit(-1);
}

void getfrom_stdin(char *dest, char *mess, char *who, char *what){
    while (scanf("%s", dest) == -1) {
        if (errno != EINTR) {
            err_handler(who, what);
        }
        printf("\n%s", mess);
    }
}



void send_packet(int sockfd, struct packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
    while (sent < sizeof(pack)) {
        res = sendto(sockfd, (void *) &pack, sizeof(pack), 0,
                     (struct sockaddr *) &servaddr, sizeof(servaddr));
        if (res == -1) {
            if (errno == EINTR) {
                printf("\nClient sendto interr. Sending...\n");
            } else {
                err_handler("Client", "sendto");
            }
        } else {
            sent = sent + res;
        }
    }
}

void send_ctrl_packet(int sockfd, struct ctrl_packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
    while (sent < sizeof(pack)) {
        res = sendto(sockfd, (void *) &pack, sizeof(pack), 0,
                     (struct sockaddr *) &servaddr, sizeof(servaddr));
        if (res == -1) {
            if (errno == EINTR) {
                printf("\nClient sendto interr. Sending...\n");
            } else {
                err_handler("Client", "sendto");
            }
        } else {
            sent = sent + res;
        }
    }
}

void retransmission_thread(union sigval arg){
    //union sigval * ptr = (union sigval *)arg;
    //printf("\nretx thr starting %d\n", timer_num);
    struct ack_thread_args *args = (struct ack_thread_args *)arg.sival_ptr;
    int timer_num = args->timer_num;
    struct window *wnd= args->wnd;
    int sockfd = args->sockfd;
    //int timer_num = arg.sival_int;
    //printf("\nretx thr starting %d\n", 4);
    int res;

    struct itimerspec timeout;

    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;

    timer_t timer = args->timers[timer_num];

    send_packet(sockfd, wnd->wnd_buff[timer_num], args->servaddr);///SCARTA CON PROBABILITA p-------------------

    ///////////////////////////////////SET TIMER
    res = timer_settime(timer, 0, &timeout, NULL);
    if(res == -1){
        err_handler("retx thread", "settime");
    }
    printf("\nritrasmissione pack: %d\n", wnd->wnd_buff[timer_num].seq_num);//todo
    pthread_exit(0);
}