#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"

/*
 * Thread avviato per il ricalcolo del TIMEOUT (circa ogni RTT)
 *
 * unlock to_mutex[0] per avvertire MAIN_THREAD che può inviare un nuovo pacchetto sample
 *
 */
void * timeout_thread(void * arg){
    struct TO_thread_args *args = (struct TO_thread_args *)arg;
    struct ack_thread_args *ack_args = (struct ack_thread_args *)args->ack_args;
    pthread_mutex_t *mux = &(ack_args->to_mutexes[0]);
    pthread_rwlock_t *to_rwlock = ack_args->to_rwlock;
    struct itimerspec *TIMEOUT = ack_args->timeout;
    struct itimerspec timeout;
    struct timespec *send_time = ack_args->send_time;
    struct timespec *ack_time = args->ack_time;
    struct timespec *dev_rtt = args->dev_rtt;
    struct timespec *estim_rtt = args->estim_rtt;
    struct timespec sample_rtt;
    long sol_nsec;
    int res;
    /*
     * calcolo SAMPLE_RTT
     */
    if (ack_time->tv_nsec - send_time->tv_nsec < 0) {
        sample_rtt.tv_sec = ack_time->tv_sec - send_time->tv_sec - 1;
        sample_rtt.tv_nsec = MAX_NANOSEC - (send_time->tv_nsec - ack_time->tv_nsec);
    } else {
        sample_rtt.tv_sec = ack_time->tv_sec - send_time->tv_sec;
        sample_rtt.tv_nsec = ack_time->tv_nsec - send_time->tv_nsec;
    }
    /*
     * calcolo ESTIMATED_RTT DEVIANCE_RTT TIMEOUT
     */
    estim_rtt->tv_nsec = (1-ALPHA)*estim_rtt->tv_nsec + ALPHA*sample_rtt.tv_nsec;
    estim_rtt->tv_sec = (1-ALPHA)*estim_rtt->tv_sec + ALPHA*sample_rtt.tv_sec;
    if((sample_rtt.tv_nsec - estim_rtt->tv_nsec) >= 0){
        dev_rtt->tv_nsec = (1-BETA)*dev_rtt->tv_nsec + BETA*(sample_rtt.tv_nsec - estim_rtt->tv_nsec);
    } else {
        dev_rtt->tv_nsec = (1-BETA)*dev_rtt->tv_nsec + BETA*(estim_rtt->tv_nsec - sample_rtt.tv_nsec);
    }
    if((sample_rtt.tv_sec - estim_rtt->tv_sec) >= 0){
        dev_rtt->tv_sec = (1-BETA)*dev_rtt->tv_sec + BETA*(sample_rtt.tv_sec - estim_rtt->tv_sec);
    } else {
        dev_rtt->tv_sec = (1-BETA)*dev_rtt->tv_sec + BETA*(estim_rtt->tv_sec - sample_rtt.tv_sec);
    }
    timeout.it_value.tv_sec = estim_rtt->tv_sec + 4*dev_rtt->tv_sec;
    sol_nsec = dev_rtt->tv_nsec + dev_rtt->tv_nsec;
    for(int i=0;i<2;i++){
        if(sol_nsec >= MAX_NANOSEC){
            timeout.it_value.tv_sec = timeout.it_value.tv_sec + 1;
            sol_nsec = sol_nsec - MAX_NANOSEC;
        }
        sol_nsec = sol_nsec + dev_rtt->tv_nsec;
    }
    if(sol_nsec >= MAX_NANOSEC){
        timeout.it_value.tv_sec = timeout.it_value.tv_sec + 1;
        sol_nsec = sol_nsec - MAX_NANOSEC;
    }
    if((estim_rtt->tv_nsec + sol_nsec) >= MAX_NANOSEC) {
        timeout.it_value.tv_sec = timeout.it_value.tv_sec + 1;
        timeout.it_value.tv_nsec = estim_rtt->tv_nsec + sol_nsec - MAX_NANOSEC;
    } else {
        timeout.it_value.tv_nsec = estim_rtt->tv_nsec + sol_nsec;
    }
    if(AUDIT_TO == 1){
        printf("\n\nest sec: %ld\nest nsec: %ld\ndev sec: %ld\ndev nsec: %ld\nTO sec: %ld\nTO nsec: %ld\n\n",
                estim_rtt->tv_sec, estim_rtt->tv_nsec, dev_rtt->tv_sec,
                dev_rtt->tv_nsec, timeout.it_value.tv_sec, timeout.it_value.tv_nsec);
    }
    /*
     * WRITE LOCK VARIABILE TIMEOUT PER COMUNICA A TUTTI I THREAD IL NUOVO VALORE
     */
    res = pthread_rwlock_wrlock(to_rwlock);
    if(res != 0){
        err_handler("Timeout_thread", "wrlock");
    }
    TIMEOUT->it_value.tv_sec = timeout.it_value.tv_sec;
    TIMEOUT->it_value.tv_nsec = timeout.it_value.tv_nsec;
    res = pthread_rwlock_unlock(to_rwlock);
    if(res != 0){
        err_handler("Timeout_thread", "unlock");
    }
    pthread_mutex_unlock(mux);
    //TODO
    if(CALC_TIMEOUT_THREAD == 0) {
        return (void*)0;
    }else {
        pthread_exit(0);
    }
}
/*
 * Gestisce la ricezione degli ACK e lo spostamento della finestra di spedizione
 */
void * ack_thread(void * arg){
    struct ack_thread_args *args = (struct ack_thread_args *)arg;
    struct window *wnd;
    int sockfd, res;
    struct ctrl_packet ack_pack;
    char who[30];
    int n;
    pthread_mutex_t *locks = args->locks;
    struct sockaddr_in addr;
    int len = sizeof(addr);
    pthread_t tid;
    struct itimerspec stop_to;
    wnd = args -> wnd;
    sockfd = args -> sockfd;

    sprintf(who, "%s", "Client ack_thread");

    stop_to.it_value.tv_sec = 0;
    stop_to.it_value.tv_nsec = 0;
    stop_to.it_interval.tv_sec = 0;
    stop_to.it_interval.tv_nsec = 0;

    pthread_mutex_t *inf_mux = args->inf_mux;
    pthread_cond_t *inf_cv = args->inf_cv;
    //ADAPTIVE == 1
        struct timespec *send_time = args->send_time;
        int *to_snum = args->to_seq_num;
        pthread_mutex_t *to_mux = args->to_mutexes;
        struct itimerspec *timeout = args->timeout;
        struct timespec estim_rtt, dev_rtt;
        struct timespec sample_rtt, ack_time;
        dev_rtt.tv_sec = 0;
        dev_rtt.tv_nsec = 0;
        estim_rtt.tv_sec = timeout->it_value.tv_sec;
        estim_rtt.tv_nsec = timeout->it_value.tv_nsec;

        /*
         * Argomenti timeout_thread
         */
        struct TO_thread_args to_args;
        to_args.ack_args = args;
        to_args.ack_time = &ack_time;
        to_args.dev_rtt = &dev_rtt;
        to_args.estim_rtt = &estim_rtt;
    /*
     * Rcezione ACK e scorrimento finestra
     */
    while(1) {

        res = recvfrom(sockfd, (void *) &ack_pack, sizeof(ack_pack), 0, (struct sockaddr *)&addr, &len);
        if (res < 0) {
            err_handler(who, "recvfrom");
        }
        if (ack_pack.ack == 1) {
            if (wnd->sup > wnd->inf) {
                if (ack_pack.ack_num <= wnd->sup && ack_pack.ack_num > wnd->inf) {
                    /*
                    *   LOCK RISORSE CONDIVISE
                    * MANIPOLATE DA SEND/ACK/RETRANSMISSION THREAD
                    */
                    //res = pthread_spin_lock(&locks[ack_pack.ack_num % N]);
                    res = pthread_mutex_lock(&locks[ack_pack.ack_num % N]);
                    if(res != 0){
                        err_handler("send thread", "spin_lock");
                    }
                    timer_settime(args->timers[ack_pack.ack_num % N], 0, &stop_to, NULL);
                    wnd->acked[ack_pack.ack_num % N] = 1;
                    if(AUDIT_ACK == 1) {
                        printf("\nACK ricevuto: %d\n", ack_pack.ack_num);
                    }
                    //res = pthread_spin_unlock(&locks[ack_pack.ack_num % N]);
                    res = pthread_mutex_unlock(&locks[ack_pack.ack_num % N]);
                    if(res != 0){
                        err_handler("send thread", "spin_unlock");
                    }
                    if(ADAPTIVE == 1){
                        if(pthread_mutex_trylock(&to_mux[1]) == 0) {
                            if (ack_pack.ack_num == *to_snum) {
                                clock_gettime(CLOCK_MONOTONIC, &ack_time);
                                if (ack_pack.TO == 1) {
                                    //TODO
                                    if(CALC_TIMEOUT_THREAD == 0) {
                                        if (AUDIT_TO == 1) {
                                            printf("\nENTRA IN TO\n");
                                        }
                                        timeout_thread((void *) &to_args);
                                        if (AUDIT_TO == 1) {
                                            printf("\nESCE DA TO\n");
                                        }
                                    } else {
                                        if (pthread_create(&tid, NULL, timeout_thread, (void *) &to_args) == -1) {
                                            err_handler(who, "pthread_create");
                                        }
                                    }
                                    if (AUDIT_TO == 1) {
                                        printf("\nACK %d TO\n", ack_pack.ack_num);
                                    }
                                } else {//TODO SAMPLE RTT NO ACK_TIME
                                    if (AUDIT_TO == 1) {
                                        printf("\n#ACK = #TO = %d MA TO = 0\n", ack_pack.ack_num);
                                    }
                                    if (ack_time.tv_nsec - send_time->tv_nsec < 0) {
                                        sample_rtt.tv_sec = ack_time.tv_sec - send_time->tv_sec - 1;
                                        sample_rtt.tv_nsec = MAX_NANOSEC - (send_time->tv_nsec - ack_time.tv_nsec);
                                    } else {
                                        sample_rtt.tv_sec = ack_time.tv_sec - send_time->tv_sec;
                                        sample_rtt.tv_nsec = ack_time.tv_nsec - send_time->tv_nsec;
                                    }
                                    if (sample_rtt.tv_sec < timeout->it_value.tv_sec) {
                                        ack_time.tv_sec = ack_time.tv_sec + timeout->it_value.tv_sec;
                                        //TODO
                                        if(CALC_TIMEOUT_THREAD == 0) {
                                            if (AUDIT_TO == 1) {
                                                printf("\nENTRA IN TO\n");
                                            }
                                            timeout_thread((void *) &to_args);
                                            if (AUDIT_TO == 1) {
                                                printf("\nESCE DA TO\n");
                                            }
                                        } else {
                                            if (pthread_create(&tid, NULL, timeout_thread, (void *) &to_args) == -1) {
                                                err_handler(who, "pthread_create");
                                            }
                                        }
                                        if (AUDIT_TO == 1) {
                                            printf("\nACK %d TO = 0 , sample_sec<timeour_sec\n", ack_pack.ack_num);
                                        }
                                    } else if(sample_rtt.tv_sec == timeout->it_value.tv_sec && sample_rtt.tv_nsec <= timeout->it_value.tv_nsec){
                                       if(ack_time.tv_nsec + timeout->it_value.tv_nsec >= MAX_NANOSEC){
                                           ack_time.tv_sec = ack_time.tv_sec + 1;
                                       } else{
                                           ack_time.tv_nsec = ack_time.tv_nsec + timeout->it_value.tv_nsec;
                                       }
                                        //TODO
                                        if(CALC_TIMEOUT_THREAD == 0) {
                                            if (AUDIT_TO == 1) {
                                                printf("\nENTRA IN TO\n");
                                            }
                                            timeout_thread((void *) &to_args);
                                            if (AUDIT_TO == 1) {
                                                printf("\nESCE DA TO\n");
                                            }
                                        } else {
                                            if (pthread_create(&tid, NULL, timeout_thread, (void *) &to_args) == -1) {
                                                err_handler(who, "pthread_create");
                                            }
                                        }
                                        if (AUDIT_TO == 1) {
                                            printf("\nACK %d TO = 0 , sample_nanosec<timeout_nanosec\n", ack_pack.ack_num);
                                        }

                                    } else {
                                        pthread_mutex_unlock(&to_mux[0]);
                                    }
                                }

                            } else {
                                pthread_mutex_unlock(&to_mux[1]);
                            }
                        }
                    }
                    if (ack_pack.ack_num == wnd->inf + 1) {
                        while ((wnd->acked[(wnd->inf + 1) % N] == 1) && (wnd->inf < wnd->sup)) {
                            wnd->inf = (wnd->inf + 1) % MAX_SEQ_NUM;
                            if(AUDIT_WND == 1) {
                                printf("\nFinestra spediti [%d, %d]\n", wnd->inf, wnd->sup);
                            }
                        }
                        pthread_mutex_lock(inf_mux);
                        pthread_cond_broadcast(inf_cv);
                        pthread_mutex_unlock(inf_mux);
                    }
                }
            }
            if (wnd->sup < wnd->inf) {
                if (ack_pack.ack_num <= wnd->sup || ack_pack.ack_num > wnd->inf) {
                    wnd->acked[ack_pack.ack_num % N] = 1;
                    printf("\nACK: %d\n", ack_pack.ack_num);//todo
                    if (ack_pack.ack_num == (wnd->inf + 1)%MAX_SEQ_NUM) {
                        while ((wnd->acked[(wnd->inf + 1) % N] == 1) && (wnd->inf > wnd->sup)) {
                            wnd->inf = (wnd->inf + 1) % MAX_SEQ_NUM;
                            printf("\nClient finestra [%d, %d]\n", wnd->inf, wnd->sup);//****************
                        }
                        if(wnd->sup > wnd->inf) {
                            while ((wnd->acked[(wnd->inf + 1) % N] == 1) && (wnd->inf < wnd->sup)) {
                                wnd->inf = (wnd->inf + 1) % MAX_SEQ_NUM;
                                printf("\nClient finestra [%d, %d]\n", wnd->inf, wnd->sup);//****************
                            }
                        }
                    }
                }
            }
        } else {
            //CONFERMA RICEZIONE SERVER
            if(ack_pack.fin == 1) {
                for(int i=0;i<N;i++){
                    args->wnd->acked[i] = 1;
                    if(timer_delete(args->timers[i]) != 0){
                        err_handler("Ack thread", "timer_delete");
                    }
                }
                if(confirm_close_connection(sockfd,addr) == 0){
                    if(AUDIT == 1) {
                        printf("\nconnection closed correctly.ACK_THREAD EXIT\n");
                    }
                    pthread_exit(0);
                } else {
                    if (AUDIT == 1) {
                        printf("\nerror in close connection.ACK_THREAD EXIT\n");
                    }
                    pthread_exit(0);
                }
            }
        }
    }
}

/*
 * IL THREAD PARTE QUANDO IL TIMER[TIMER_NUM] SCADE INDICANDO UN TIMEOUT
 */
void retransmission_thread(union sigval arg){
    struct rtx_thread_args *rtx_args = (struct rtx_thread_args *)arg.sival_ptr;
    struct ack_thread_args *args = rtx_args->shared;
    int timer_num = rtx_args->timer_num;
    struct window *wnd= args->wnd;
    int sockfd = args->sockfd;
    pthread_rwlock_t *to_rwlock = args->to_rwlock;
    int res;
    pthread_mutex_t * locks = args->locks;

    struct itimerspec *timeout = args->timeout;
    struct itimerspec timer_state;

    struct timespec *send_time = args->send_time;
    int *to_snum = args->to_seq_num;
    pthread_mutex_t *to_mux = args->to_mutexes;
    timer_t timer = args->timers[timer_num];


    //spinlock
    //if timer disarmed && !ACK
    //res = pthread_spin_lock(&locks[timer_num]);
    res = pthread_mutex_lock(&locks[timer_num]);
    if(res != 0){
        err_handler("send thread", "spin_lock");
    }
    timer_gettime(timer, &timer_state);
    if(((timer_state.it_value.tv_sec) == 0 && (timer_state.it_value.tv_nsec) == 0)  &&  (wnd->acked[timer_num] != 1)) {
        /////////////////////////////////////////////////////////////////////////////////////
        if(ADAPTIVE == 1){
            if((pthread_mutex_trylock(&to_mux[0])) == 0){
                if(AUDIT_TO){
                    printf("\nrtx sending sample_rtt packet %d\n", wnd->wnd_buff[timer_num].seq_num);
                    res = pthread_rwlock_rdlock(to_rwlock);
                    if(res != 0){
                        err_handler("Retransmission thread", "rdlock");
                    }
                    printf("\nrtx_TO: sec %ld, nsec %ld\n", timeout->it_value.tv_sec, timeout->it_value.tv_nsec);
                    res = pthread_rwlock_unlock(to_rwlock);
                    if(res != 0){
                        err_handler("Retransmission thread", "unlock");
                    }
                }
                *to_snum = wnd->wnd_buff[timer_num].seq_num;
                wnd->wnd_buff[timer_num].TO = 1;
                clock_gettime(CLOCK_MONOTONIC, send_time);
                pthread_mutex_unlock(&to_mux[1]);
                send_packet(sockfd, wnd->wnd_buff[timer_num], *(args->servaddr));
                wnd->wnd_buff[timer_num].TO = 0;
            } else {
                send_packet(sockfd, wnd->wnd_buff[timer_num], *(args->servaddr));
            }
        }
        else{
            send_packet(sockfd, wnd->wnd_buff[timer_num], *(args->servaddr));
        }
        res = pthread_rwlock_rdlock(to_rwlock);
        if(res != 0){
            err_handler("Retransmission thread", "rdlock");
        }
        res = timer_settime(timer, 0, timeout, NULL);
        if(res == -1){
            err_handler("retx thread", "settime");
        }
        res = pthread_rwlock_unlock(to_rwlock);
        if(res != 0){
            err_handler("Retransmission thread", "unlock");
        }
        if(AUDIT_SEND == 1) {
            printf("\nritrasmissione pack: %d\n", wnd->wnd_buff[timer_num].seq_num);
        }
    }
    //spinunlock
    //res = pthread_spin_unlock(&locks[timer_num]);
    res = pthread_mutex_unlock(&locks[timer_num]);
    if(res != 0){
        err_handler("send thread", "spin_unlock");
    }

    pthread_exit(0);
}
