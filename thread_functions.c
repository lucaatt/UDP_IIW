#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"

/*
 * THREAD INVIA PACCHETTI LETTI E RESI DISPONIBILI DAL MAIN IN ARGS->READY[] (INIZIA A RIEMPIRE DA  POSIZIONE [0])
 * PRIMA DI INVIARE CONTROLLA CHE #SEQ ENTRO FINESTRA
 * QUANDO INVIA ATTIVA TIMER PER TIMEOUT E LIBERA SLOT BUFFER PER INSERIMENTO NUOVO PACCHETTO (SLOT[posizine])
 *
 * CONDIVISI DA ACK/RETRANSMISSION_THREAD
 *  ARGS-->  locks: usato per lock risorse condivise con ack/retransmission_thread ( wnd_acked[] , timers[] )
 *                  lock ==>  prima di send_packet e start timer (timer_settime)
 *           wnd:  struct window ==> INF: usato per verificare #SEQ entro limiti finestra  (INF <= SUP)
 *                                     [ultimo #seq in ordine con ack ricevuto]
 *                                   SUP: ( INF < #ACK <= SUP ) -> ack solo pacchetti inviati
 *                                          (INF <= SUP)   -> quando finestra (INF) scorre    ==>  scorre in ack_thread
 *                                         [ultimo(piu grande) #SEQ inviato]
 *                                   WND_BUFF: contiene pacchetti spediti
 *                                              [ritrasmessi se timer nella stessa posizione scade]
 *                                   ACKED[posizione]:  settato a 0 quando pacchetto spedito
 *                                                         [settato a 1 quando ACK ricevuto] -> da ack_thread
 *          ready[]: buffer pacchetti letti dal main pronti per spedizione
 *          slots[]: indica quali posizioni di READY sono libere(vuote)[0] -> pacchetto spedito/ nuovo non disponibile
 *                                                   o occupate  [1]     -> pacchetto da spedire
 *          timers[]: array di TIMER -> timer[#pos] avviato quando pacchetto #pos spedito
 *          servaddr: contiene indirizzo destinatario (server --> PUT)
 *                                                     (client --> GET)
 *          sockfd: descrittore della SOCKET
 *
 */
void * send_thread(void * arg){
    int slot = 0;
    int res;
    struct send_thread_args *args = (struct send_thread_args *)arg;
    struct itimerspec timeout;
    pthread_spinlock_t *locks = args->locks;

    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;


    while(1) {
        while(args->slots[slot] == 0);
        if((args->ready[slot].seq_num - args->wnd->inf) > 0) {
            while((args->ready[slot].seq_num - args->wnd->inf) > N);
        }
        else if((args->ready[slot].seq_num - args->wnd->inf) < 0) {
            while(((MAX_SEQ_NUM - (args->wnd->inf - args->ready[slot].seq_num)) > N) &&
                  ((args->ready[slot].seq_num - args->wnd->inf) < 0));
        }

        /*
         * LOCK RISORSE CONDIVISE
         * MANIPOLATE DA SEND/ACK/RETRANSMISSION THREAD
         */
        res = pthread_spin_lock(&locks[args->ready[slot].seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_lock");
        }
        args->wnd->wnd_buff[args->ready[slot].seq_num % N] = args->ready[slot];
        args->wnd->acked[args->ready[slot].seq_num % N] = 0;
        args->wnd->sup = args->ready[slot].seq_num;
        send_packet(args->sockfd, args->ready[slot], args->servaddr);
        res = timer_settime(args->timers[args->ready[slot].seq_num % N], 0, &timeout, NULL);
        if(res == -1){
            err_handler("send thread", "settime");
        }
        printf("\ninviato pack: %d\n", args->ready[slot].seq_num);//todo
        res = pthread_spin_unlock(&locks[args->ready[slot].seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_unlock");
        }


        args->slots[slot] = 0;
        if(args->ready[slot].last == 1) {
            printf("\nsend_thread exit\n");
            pthread_exit(0);
        }
        slot = (slot + 1) % READY_SIZE;
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
    pthread_spinlock_t *locks = args->locks;

    struct itimerspec stop_to;

    stop_to.it_value.tv_sec = 0;
    stop_to.it_value.tv_nsec = 0;
    stop_to.it_interval.tv_sec = 0;
    stop_to.it_interval.tv_nsec = 0;

    sprintf(who, "%s", "Client ack_thread");
    wnd = args -> wnd;
    sockfd = args -> sockfd;

    while(1) {//GESTIRE MAX_SEQ_NUM

        res = recvfrom(sockfd, (void *) &ack_pack, sizeof(ack_pack), 0, NULL, NULL);
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
                    res = pthread_spin_lock(&locks[ack_pack.ack_num % N]);
                    if(res != 0){
                        err_handler("send thread", "spin_lock");
                    }
                    timer_settime(args->timers[ack_pack.ack_num % N], 0, &stop_to, NULL);
                    wnd->acked[ack_pack.ack_num % N] = 1;
                    printf("\nACK: %d\n", ack_pack.ack_num);//todo
                    res = pthread_spin_unlock(&locks[ack_pack.ack_num % N]);
                    if(res != 0){
                        err_handler("send thread", "spin_unlock");
                    }
                    if (ack_pack.ack_num == wnd->inf + 1) {
                        while ((wnd->acked[(wnd->inf + 1) % N] == 1) && (wnd->inf < wnd->sup)) {
                            wnd->inf = (wnd->inf + 1) % MAX_SEQ_NUM;
                            printf("\nClient finestra [%d, %d]\n", wnd->inf, wnd->sup);//****************
                        }
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
                printf("\nTHREAD ESCE\n");
                pthread_exit(0);
            }
        }
    }
}

void retransmission_thread(union sigval arg){
    struct ack_thread_args *args = (struct ack_thread_args *)arg.sival_ptr;
    int timer_num = args->timer_num;
    struct window *wnd= args->wnd;
    int sockfd = args->sockfd;
    int res;
    pthread_spinlock_t * locks = args->locks;

    struct itimerspec timeout;
    struct itimerspec timer_state;

    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;

    timer_t timer = args->timers[timer_num];


    //spinlock
    //if timer disarmed && !ACK
    res = pthread_spin_lock(&locks[timer_num]);
    if(res != 0){
        err_handler("send thread", "spin_lock");
    }
    timer_gettime(timer, &timer_state);
    if(((timer_state.it_value.tv_sec) == 0 && (timer_state.it_value.tv_nsec) == 0)  &&  (wnd->acked[timer_num] != 1)) {
        send_packet(sockfd, wnd->wnd_buff[timer_num], args->servaddr);
        res = timer_settime(timer, 0, &timeout, NULL);
        if(res == -1){
            err_handler("retx thread", "settime");
        }
        printf("\nritrasmissione pack: %d\n", wnd->wnd_buff[timer_num].seq_num);//todo
    }
    //spinunlock
    res = pthread_spin_unlock(&locks[timer_num]);
    if(res != 0){
        err_handler("send thread", "spin_unlock");
    }

    pthread_exit(0);
}