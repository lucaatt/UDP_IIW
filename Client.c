#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"
#include "thread_functions.h"

#define IP_LOOPBACK "127.0.0.1"
#define IP_SIZE 16

void event_handler() {
    printf("\n");
}
/*
 * Serve a stabilire una connessione con il SERVER in ascolto su un #PORTA predefinito
 * che ricevuto il primo mess dal CLIENT genera una nuova SOCKET con diverso #PORTA per
 * gestire più richieste in concorrenza.
 * HANDSHAKE necessario per ottenere nuova PORTA del SERVER, inizializzare #SEQUENZA ecc
 *
 *
 * ctrl_pack   -> deve contenere CMD  (conterrà alla fine ack_num(#SEQ SERVER)
 *                                  e seq_num(#SEQ CLIENT da incrementare nel prossimo pacchetto spedito))
 * servaddr    -> deve contenere #PORTA (NOTO) e IP SERVER
 *                 (conterrà alla fine NUOVO #PORTA SERVER)
 *                 (USARE SERVADDR NELLE SUCCESSIVE "SEND_PACKET")
*/
int handshake_client(int sockfd, struct ctrl_packet *ctrl_pack, struct sockaddr_in *servaddr, struct timespec *sample_rtt) {
    struct ctrl_packet rcv_pack;
    ctrl_pack->seq_num = 0;
    ctrl_pack->syn = 1;
    int len, res;
    struct timespec connection_timeout = {2,0};
    struct timespec send_time, ack_time;//////////////////////////////////////////
    int attempts = 0;

    ////////////////////////////////////////////////////////////////////////////
    if(sample_rtt != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &send_time);
    }
    send_ctrl_packet(sockfd, *ctrl_pack, *servaddr);

    len = sizeof(*servaddr);

    /*
     * IMPOSTA TIMEOUT DI CONNESSIONE
     *
     * VIENE INCREMENTATO AD OGNI TENTATIVO (MAX TENTATIVI = 4)
     */
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler("Handshake", "setsockopt");
    }
    /*
     * Dopo aver ricevuto correttamente il pacchetto dal SERVER "servaddr" conterrà il
     * nuovo #PORTA che verrà utilizzato dal CLIENT per inviare i mess successivi
    */
    while(recvfrom(sockfd, (void *) &rcv_pack, sizeof(rcv_pack), 0, (struct sockaddr *) servaddr, &len) < 0){
        if(errno != EINTR){
            if(errno == EWOULDBLOCK){
                attempts++;
                if(attempts == 4){
                    printf("\n%d attempts to connect with Server. Try again later.\n\n", attempts);
                    exit(0);
                }
                printf("\nHandshake attempt number %d. Server not responding...", attempts);
                connection_timeout.tv_sec = (connection_timeout.tv_sec)*2;
                if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
                    err_handler("Handshake", "setsockopt");
                }
                if(sample_rtt != NULL) {
                    clock_gettime(CLOCK_MONOTONIC, &send_time);
                }
                send_ctrl_packet(sockfd, *ctrl_pack, *servaddr);
            }
            else{
                err_handler("Handshake", "recvfrom");
            }
        }
    }


/*
 * CALCOLO SAMPLE RTT
 */
    if(sample_rtt != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &ack_time);
        if (ack_time.tv_nsec - send_time.tv_nsec < 0) {
            sample_rtt->tv_sec = ack_time.tv_sec - send_time.tv_sec - 1;
            sample_rtt->tv_nsec = 1000000000 - (send_time.tv_nsec - ack_time.tv_nsec);
        } else {
            sample_rtt->tv_sec = ack_time.tv_sec - send_time.tv_sec;
            sample_rtt->tv_nsec = ack_time.tv_nsec - send_time.tv_nsec;
        }
    }


    connection_timeout.tv_sec = 0;
    connection_timeout.tv_nsec = 0;
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler("Handshake", "setsockopt");
    }
    if (rcv_pack.ack == 1) {
        if (rcv_pack.ack_num != ctrl_pack->seq_num) {
            return -1;
        } else {
            ctrl_pack->ack_num = rcv_pack.seq_num;
        }
    } else {
        return -1;
    }
    if (ctrl_pack->cmd == 1 || ctrl_pack->cmd == 2) {
        /*
         * ctrl pack ha #SEQ e #ACK(seq server)
         * servaddr ha nuovo #PORT del server
         * se PUT ultimo ACK handshake in piggyback con FILENAME
         */
        ctrl_pack->syn = 0;
        if(AUDIT_SEND == 1){
            printf("\nHANDSHAKE CONCLUSO\n");
        }
        return 0;
    }
    return 0;
}

void put_function(int sockfd, int sem_stdout, struct sockaddr_in servaddr){
    /*
     * Spedizione PACCHETTI
     */
    struct packet pack;
    struct ctrl_packet ctrl_pack;
    //Usati per attesa Main Thread scorrimento finestra
    pthread_cond_t inf_cv;
    pthread_mutex_t inf_mux;
    pthread_cond_init(&inf_cv, NULL);
    pthread_mutex_init(&inf_mux, NULL);
    /*
     * Gestione File
     */
    int actread;
    int fd;
    char *filename, buff[1024];
    /*
     * Gestione Finestra di spedizione e ritrasmissione
     */
    struct window wnd;
    //Retransmission thread
    struct rtx_thread_args rtx_args[N];
    struct sigevent sig_to;
    //ACK thread
    struct ack_thread_args args;
    //condivise
    pthread_mutex_t locks[N];
    //TIMEOUT
    timer_t timers[N];
    struct itimerspec timeout;
    struct timespec send_t;
    pthread_mutex_t to_mux[2];
    pthread_rwlock_t to_rwlock;
    int to_snum;
    struct timespec sample_rtt;
    /*
     * Altro
     */
    int res;
    char who[11];
    struct sembuf s_stdo;
    pid_t ppid;
    pthread_t tid;
    int n;

    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    srand(time(0));//NECESSARIO PER GENERARE OGNI VOLTA SEQUENZE DIVERSE DI NUMERI PSEUDORANDOM

    ppid = getppid();
    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&wnd, 0, sizeof(wnd));
    memset((void*)&pack, 0, sizeof(pack));

    sprintf(who, "%s", "Client PUT");

    /*
    * TIMER MUTEX
    * inizializzazione mutex -> lock prima di spedire pacchetto reativo al TIMER
    *                              unlock dopo START(send_thread o retrans_thread) e STOP(ack_thread) TIMER
    *                                                wnd_acked = 0                       wnd_acked = 1
    */
    for(int n=0;n<N;n++){
        wnd.acked[n] = 0;
        res = pthread_mutex_init(&locks[n], NULL);
        if(res == -1){
            err_handler(who, "mutex init");
        }
    }
    /*
     * Argomenti da passare ad ack_thread e retransmission_thread
     */
    sig_to.sigev_notify = SIGEV_THREAD;
    sig_to.sigev_notify_function = &retransmission_thread;

    args.sockfd = sockfd;
    args.wnd = &wnd;
    args.timers = timers;
    args.locks = locks;
    args.servaddr = &servaddr;
    args.send_time = &send_t;
    args.timeout = &timeout;
    args.to_mutexes = to_mux;
    args.to_seq_num = &to_snum;
    args.inf_cv = &inf_cv;
    args.inf_mux = &inf_mux;
    args.to_rwlock = &to_rwlock;
    for(int i=0;i<N;i++){
        sig_to.sigev_value.sival_ptr = (void*)&rtx_args[i];
        rtx_args[i].timer_num = i;
        rtx_args[i].shared = &args;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
    }

    /*
     * INIZIO CONNESSIONE
     */
    /*
     * CMD 1 indica al Server richiesta PUT
     */
    ctrl_pack.cmd = 1;
    /*
     * Inserire nome del File da spedire al Server
     */
    filename = (char *)malloc(MAX_FILENAME_SIZE);
    if(filename == NULL) {
        err_handler(who, "malloc");
    }
    printf("\nEnter filename to send: ");
    getfrom_stdin(filename, "Enter filename to send: ", who, "scanf");
    res = semop(sem_stdout, &s_stdo, 1);
    if(res == -1){
        err_handler(who, "semop");
    }
    //Apertura FILE
    sprintf(buff, "./client_files/%s", filename);
    while((fd = open(buff, O_RDONLY)) == -1){
        if(errno != EINTR){
            err_handler(who, "open");
        }
    }
    res = sprintf(pack.data, "%s", filename);
    if(res < 0){
        err_handler(who, "sprintf");
    }
    /*
     * HANDSHAKE CON SERVER
     */
    if(ADAPTIVE == 0) {
        res = handshake_client(sockfd, &ctrl_pack, &servaddr, NULL);
        if(res == -1){
            err_handler(who, "handshake_client");
        }
        timeout.it_value.tv_sec = DEF_TO_SEC;
        timeout.it_value.tv_nsec = DEF_TO_NSEC;
    }
    else if(ADAPTIVE == 1){
        res = handshake_client(sockfd, &ctrl_pack, &servaddr, &sample_rtt);
        if(res == -1){
            err_handler(who, "handshake_client");
        }
        timeout.it_value.tv_sec = sample_rtt.tv_sec;
        timeout.it_value.tv_nsec = sample_rtt.tv_nsec;
        if(AUDIT_TO == 1){
            printf("\ndopo handshake TO sec %ld, nsec %ld\n", timeout.it_value.tv_sec, timeout.it_value.tv_nsec);
        }
        for(int i=0;i<2;i++){
            res = pthread_mutex_init(&to_mux[i], NULL);
            if(res == -1){
                err_handler(who, "mutex init");
            }
            pthread_mutex_lock(&to_mux[i]);
        }
    }
    pthread_rwlock_init(&to_rwlock, NULL);
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;
    /*
     * INIZIALIZZAZIONE #SEQ E FINESTRA DOPO HANDSHAKE
     */
    pack.seq_num = ctrl_pack.seq_num + 1;
    pack.ack_num = ctrl_pack.ack_num;
    pack.ack = 1;
    wnd.inf = pack.seq_num - 1;
    wnd.sup = pack.seq_num;
    /*
     * Avvio ack_thread
     */
    res = pthread_create(&tid, NULL, ack_thread, (void*)&args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    /*----------------------------------------------
     * INIZIO Invio pacchetto con nome File
     */
    wnd.wnd_buff[pack.seq_num % N] = pack;
    wnd.acked[pack.seq_num % N] = 0;
    if(ADAPTIVE == 1) {
        to_snum = pack.seq_num;
        pack.TO = 1;
        clock_gettime(CLOCK_MONOTONIC, &send_t);
        /*
         * Avverti ack_thread avvenuto invio pacchetto sample_rtt
         */
        pthread_mutex_unlock(&to_mux[1]);
        send_packet(sockfd, pack, servaddr);
        pack.TO = 0;
    }
    else if(ADAPTIVE == 0){
        send_packet(sockfd, pack, servaddr);
    }
    /*
     * READ LOCK DELLA VARIABILE TIMEOUT
     */
    res = pthread_rwlock_rdlock(&to_rwlock);
    if(res != 0){
        err_handler(who, "rdlock");
    }
    res = timer_settime(timers[pack.seq_num % N], 0, &timeout, NULL);
    if(res == -1){
        err_handler("send thread", "settime");
    }
    res = pthread_rwlock_unlock(&to_rwlock);
    if(res != 0){
        err_handler(who, "rdlock");
    }
    /*
     * FINE invio
     *///-----------------------------------------------
     /*
      * Attesa ACK
      */
    while((wnd.sup - wnd.inf) != 0){
        pthread_mutex_lock(&inf_mux);
        pthread_cond_wait(&inf_cv, &inf_mux);
        pthread_mutex_unlock(&inf_mux);
    }
    /*
     * SPEDIZIONE FILE
     */
    pack.ack = 0;
    memset((void*)pack.data, 0, sizeof(DATA_SIZE));
    while((res = read(fd, (void*)pack.data, DATA_SIZE - 1)) != 0) {
        if (res == -1) {
            if (errno != EINTR) {
                err_handler(who, "read");
            }
        }
        pack.data[res] = '\0';
        pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;
        /*if(res < DATA_SIZE - 1){
            pack.last = 1;
        }*/
        /*
         * Attesa scorrimento finestra spedizione
         */
        while(pack.seq_num > wnd.inf + N){
            pthread_mutex_lock(&inf_mux);
            pthread_cond_wait(&inf_cv, &inf_mux);
            pthread_mutex_unlock(&inf_mux);
        }
        /*-------------------------------------------------------------------------------------------
         * LOCK-> gestione finestra
         */
        res = pthread_mutex_lock(&locks[pack.seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_lock");
        }
        //Array di pacchetti spediti senza ACK
        //In caso di TIMEOUT ritrasmessi da RETRANSMISSION_THREAD
        wnd.wnd_buff[pack.seq_num % N] = pack;
        wnd.acked[pack.seq_num % N] = 0;
        wnd.sup = pack.seq_num;
        /*
         * Se TIMEOUT ADATTATIVO
         *
         * controlla se dare inizio a un nuovo calcolo del TO
         * (mutex_trylock (to_mux[0]) -> sbloccato da ack_thread quando riceve ACK del sample_rtt precedente e calcola TO)
         *
         * indica ad ack_thread: quale pacchetto usato per calcolo SAMPLE_RTT (to_snum)
         *                       istante di invio (send_t)
         *                       avvenuto invio del pacchetto sample (unlock -> to_mux[1])
         *
         */
        if(ADAPTIVE == 1){
            if((pthread_mutex_trylock(&to_mux[0])) == 0){
                if(AUDIT_TO){
                    printf("\nsending sample_rtt packet %d\n", pack.seq_num);
                    res = pthread_rwlock_rdlock(&to_rwlock);
                    if(res != 0){
                        err_handler(who, "rdlock");
                    }
                    printf("\nTO: sec %ld, nsec %ld\n", timeout.it_value.tv_sec, timeout.it_value.tv_nsec);
                    res = pthread_rwlock_unlock(&to_rwlock);
                    if(res != 0){
                        err_handler(who, "rdlock");
                    }
                }
                to_snum = pack.seq_num;
                pack.TO = 1;
                clock_gettime(CLOCK_MONOTONIC, &send_t);
                pthread_mutex_unlock(&to_mux[1]);
            }
        }
        send_packet(sockfd, pack, servaddr);
        if(ADAPTIVE == 1){
            pack.TO = 0;
        }
        /*
         * Avvio timer TIMEOUT
         */
        res = pthread_rwlock_rdlock(&to_rwlock);
        if(res != 0){
            err_handler(who, "rdlock");
        }
        res = timer_settime(timers[pack.seq_num % N], 0, &timeout, NULL);
        if(res == -1){
            err_handler("send thread", "settime");
        }
        res = pthread_rwlock_unlock(&to_rwlock);
        if(res != 0){
            err_handler(who, "rdlock");
        }
        res = pthread_mutex_unlock(&locks[pack.seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_unlock");
        }
        /*
         * Unlock risorse
         *///------------------------------------------------------------------------------------------------------
    }
    if(pack.last != 1){
        pack.last = 1;
        pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;
        while(pack.seq_num > wnd.inf + N){
            pthread_mutex_lock(&inf_mux);
            pthread_cond_wait(&inf_cv, &inf_mux);
            pthread_mutex_unlock(&inf_mux);
        }
        res = pthread_mutex_lock(&locks[pack.seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_lock");
        }
        wnd.wnd_buff[pack.seq_num % N] = pack;
        wnd.acked[pack.seq_num % N] = 0;
        wnd.sup = pack.seq_num;
        send_packet(sockfd, pack, servaddr);
        res = pthread_rwlock_rdlock(&to_rwlock);
        if(res != 0){
            err_handler(who, "rdlock");
        }
        res = timer_settime(timers[pack.seq_num % N], 0, &timeout, NULL);
        if(res == -1){
            err_handler("send thread", "settime");
        }
        res = pthread_rwlock_unlock(&to_rwlock);
        if(res != 0){
            err_handler(who, "unlock");
        }
        res = pthread_mutex_unlock(&locks[pack.seq_num % N]);
        if(res != 0){
            err_handler("send thread", "spin_unlock");
        }
    }
    pthread_join(tid, NULL);//Aspetta conferma da Server e chiusura connesione (ack_thread)
    printf("\nClient FILE %s sent\n", filename);
    kill(ppid, SIGUSR1);
    exit(0);
}

/*
 * Chiamata dal main se l'utente ha inserito il comando "get"
 *
 * sockfd     -> ID della socket lato CLIENT utilizzata per lo scambio di mess con SERVER
 * sem_stdout -> semaforo da rilasciare dopo scanf(filename) per permettere al mainThread di ricevere altri comandi
 *                  dall'utente (GESTITI IN CONCORRENZA)
 * servaddr   -> struct che contiene #PORTA(NOTO) e indirizzo IP del SERVER
 *                  (DOPO HANDSHAKE SERVADDR CONTERRA NUOVO #PORTA SERVER USATO PER QUESTA CONNESSIONE)
 *                  (SERVER UTILIZZERA VECCHIO #PORTA PER RICEVERE ALTRE RICHIESTE CONCORRENTI)
 */
void get_function(int sockfd, int sem_stdout, struct sockaddr_in servaddr){
    int fd, len, res;
    FILE *file;
    char path[1520];
    char who[40];
    char * filename;
    struct ctrl_packet ack_pack;//pacchetto per inviare ACK (e all'inizio HANDSHAKE)
    struct window wnd;
    int var;
    int n;

    char buff[1024];
    struct packet pack;//pacchetto per riceve FILE dal SERVER (e inviare filename)
    struct sembuf s_stdo, w_stdo;
    pid_t ppid;

    srand(time(0));//NECESSARIO PER GENERARE OGNI VOLTA SEQUENZE DIVERSE DI NUMERI PSEUDORANDOM

    memset((void*)&ack_pack, 0, sizeof(ack_pack));
    memset((void*)&pack, 0, sizeof(pack));
    memset((void*)&wnd, 0, sizeof(wnd));

    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }

    sprintf(who, "%s", "Client GET");

    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    filename = (char *)malloc(MAX_FILENAME_SIZE);
    if(filename == NULL) {
        err_handler(who, "malloc");
    }

    printf("\nEnter filename to get: ");

    getfrom_stdin(filename, "Enter filename to get: ", who, "scanf");
    res = semop(sem_stdout, &s_stdo, 1);
    if(res == -1){
        err_handler(who, "semop");
    }

    ack_pack.cmd = 2;/////////LETTO DA SERVER (VALORE 2 INDICA GET)
    /////CMD USATO IN HANDSHAKE!!!
    /*
     * Dopo HANDSHAKE ack_pack.seq_num = 0 (#SEQ CLIENT)
     *                 ack_pack.ack_num = 0 (#SEQ SERVER)  ==>  non necessariamente uguali
     *                                                          (#SEQ SERVER aumenterà con ricezione FILE)
     *                                                           (#SEQ CLIENT aumenterà solo per mandare FILENAME)
     */
    res = handshake_client(sockfd, &ack_pack, &servaddr, NULL);
    if(res == -1){
        err_handler(who, "handshake_client");
    }
    res = sprintf(pack.data, "%s", filename);///ULTIMO PACCHETTO HANDSHAKE CHE OLTRE ACK CONTIENE FILENAME
    if(res < 0){
        err_handler(who, "sprintf");
    }
    pack.seq_num = ack_pack.seq_num + 1;//#SEQ + 1 = 1   ultimo pacchetto mandato in handshake(ack_pack) [#SEQ = 0]
    pack.ack_num = ack_pack.ack_num;// #ACK relativo a #SEQ pacchetto SERVER in attesa dell' ACK
    pack.ack = 1;                 ///indica che pacchetto contiene ACK

    wnd.inf = pack.ack_num;
    send_packet(sockfd, pack, servaddr);
    /*
    * ACK DAL SERVER E CONFERMA ESISTENZA FILE
    *
    */
    struct timespec connection_timeout = {2,0};
    int attempts = 0;
    len = sizeof(servaddr);
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler("Get function", "setsockopt");
    }
    while(recvfrom(sockfd, (void *)&pack, sizeof(pack), 0, (struct sockaddr *)&servaddr, &len) < 0){
        if(errno != EINTR){
            if(errno == EWOULDBLOCK){
                attempts++;
                if(attempts == 4){
                    printf("\nServer not responding. Closing connection...\n\n");
                    exit(0);
                }
                connection_timeout.tv_sec = (connection_timeout.tv_sec)*2;
                if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
                    err_handler("Handshake", "setsockopt");
                }
                send_packet(sockfd, pack, servaddr);
            }
            else{
                err_handler("Handshake", "recvfrom");
            }
        }
    }
    connection_timeout.tv_sec = 0;
    connection_timeout.tv_nsec = 0;
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler("Handshake", "setsockopt");
    }
    if(pack.ack == 0 && pack.fin == 1){
        printf("\nMessage from SERVER: FILE %s does not exist\n", filename);
        if(confirm_close_connection(sockfd,servaddr) == 0){
            if(AUDIT == 1) {
                printf("\nconnection closed correctly.ACK_THREAD EXIT\n");
            }
        } else {
            if (AUDIT == 1) {
                printf("\nerror in close connection.ACK_THREAD EXIT\n");
            }
        }
        kill(ppid, SIGUSR1);
        exit(0);
    }
    /*
     * SE FILE ESISTE PACK CONTERRA GIA PRIMO PEZZO FILE
     */
    wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
    wnd.acked[pack.seq_num % N] = 1;
    ack_pack.ack_num = pack.seq_num;
    ack_pack.ack = 1;//////////////////DA QUI IN POI ACK_PACK USATO SOLO PER ACK
    if(ADAPTIVE == 1){
        if(pack.TO == 1){
            ack_pack.TO = 1;
            if(AUDIT_TO == 1){
                printf("\nACK TO packet %d", ack_pack.ack_num);
            }
        }
    }
    send_ctrl_packet(sockfd, ack_pack, servaddr);
    ack_pack.TO = 0;
    /*
     * FINESTRA RICEZIONE INIZIALIZZAZZIONE
     */
    wnd.sup = pack.seq_num;//SUP -> #SEQ (entro limiti finestra) MAGGIORE TRA I PACCHETTI RICEVUTI(anche fuori ordine)
    //wnd.inf = pack.seq_num;//INF -> ULTIMO PACCHETTO RICEVUTO IN ORDINE E LETTO
    /*
     * CREAZIONE FILE => AGGIUNGERE RICHIESTA SOVRASCRITTURA SE GIA ESISTE NEL CLIENT???
     */
    printf("\nClient saving file %s from Server...\n", filename);
    kill(ppid, SIGUSR1);
    sprintf(path, "./client_files/%s", filename);
    while((fd = open(path, O_CREAT|O_RDWR|O_TRUNC, 0660)) == -1){
        if(errno != EINTR){
            err_handler(who, "open");
        }
    }
    /*
     * SCRITTURA SU FILE
     */
    file = fdopen(fd, "w+");
    if(file == NULL){
        err_handler(who, "fdopen");
    }
    if(pack.seq_num == wnd.inf + 1) {
        res = fprintf(file, "%s", pack.data);
        if (res < 0) {
            err_handler(who, "fprintf");
        }
        fflush(file);
        if (wnd.wnd_buff[(wnd.inf + 1) % N].last == 1) {/////ESCI SE TUTTO IN BUFF SALVATO
            printf("\nServer saved %s successfully\n", path);
            if(start_close_connection(sockfd, wnd.inf + 1, servaddr) == 0){
                if(AUDIT == 1){
                    printf("\nconnection closed correctly\n");
                }
                exit(0);
            } else{
                if(AUDIT == 1){
                    printf("\nerror in close connection\n");
                }
                exit(-1);
            }
        }
        wnd.acked[(wnd.inf + 1) % N] = 0;
        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
    }
    /*
     * RICEZIONE, SCRITTURA E ACK RESTO DEL FILE DAL SERVER
     *
     */
    struct timespec rcv_timeout = {MAX_WAITING_TIME_SEC,0};
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&rcv_timeout, sizeof(rcv_timeout))<0){
        err_handler("Get function", "setsockopt");
    }
    while (1) {
        while(recvfrom(sockfd, (void *) &pack, sizeof(pack), 0,(struct sockaddr *) &servaddr, &len) < 0) {
            if(errno != EINTR){
                if(errno == EWOULDBLOCK){
                    printf("\nServer stopped sending for too long. Closing connection\n");
                    if(start_close_connection(sockfd, wnd.inf + 1, servaddr) == 0){
                        if(AUDIT == 1){
                            printf("\nconnection closed correctly\n");
                        }
                        exit(0);
                    } else{
                        if(AUDIT == 1){
                            printf("\nerror in close connection\n");
                        }
                        exit(-1);
                    }
                }
                else{
                    err_handler("Get function", "recvfrom");
                }
            }
        }
        if(AUDIT == 1) {
            printf("\nricevuto pack: %d\n", pack.seq_num);
        }
        if (((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf) {
            if ((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)) {
                if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                ack_pack.ack_num = pack.seq_num;
                if(ADAPTIVE == 1){
                    if(pack.TO == 1){
                        ack_pack.TO = 1;
                        if(AUDIT_TO == 1){
                            printf("\nACK TO packet %d", ack_pack.ack_num);
                        }
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                    }
                } else {
                    send_ctrl_packet(sockfd, ack_pack, servaddr);
                }
                if (pack.seq_num == wnd.inf + 1) {
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                        //scrivi su file(ACKED A 0) da wnd_buff THREAD
                        res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf + 1) % N].data);
                        if(AUDIT == 1) {
                            printf("\nscritto: %d\n", wnd.inf + 1);
                        }
                        if (res < 0) {
                            err_handler(who, "fprintf");
                        }
                        //fflush(file);
                        if (wnd.wnd_buff[(wnd.inf + 1) % N].last == 1) {/////ESCI SE TUTTO IN BUFF SALVATO
                            printf("\nClient saved %s successfully\n", path);
                            if(start_close_connection(sockfd, wnd.inf + 1, servaddr) == 0){
                                if(AUDIT == 1){
                                    printf("\nconnection closed correctly\n");
                                }
                                exit(0);
                            } else{
                                if(AUDIT == 1){
                                    printf("\nerror in close connection\n");
                                }
                                exit(-1);
                            }
                        }
                        wnd.acked[(wnd.inf + 1) % N] = 0;
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;

                    }
                }
            } else if (pack.seq_num <= wnd.inf) {
                ack_pack.ack_num = pack.seq_num;
                if(ADAPTIVE == 1){
                    if(pack.TO == 1){
                        ack_pack.TO = 1;
                        if(AUDIT_TO == 1){
                            printf("\n#SEQ<=INF; ACK TO packet %d", ack_pack.ack_num);
                        }
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                    }
                }
                else{
                    send_ctrl_packet(sockfd, ack_pack, servaddr);
                }
            }
        } /*else if (((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf) {
            if (pack.seq_num < ((wnd.inf + N) % MAX_SEQ_NUM)) {
                if (wnd.sup >= wnd.inf) {
                    wnd.sup = pack.seq_num;
                } else if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, addr);
                if (pack.seq_num == ((wnd.inf + 1) % MAX_SEQ_NUM)) {
                    var = wnd.sup;
                    if (wnd.sup < wnd.inf) {
                        var = MAX_SEQ_NUM + wnd.sup;
                    }
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < var)) {
                        //scrivi su file(ACKED A 0) da wnd_buff THREAD
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
                        if (wnd.sup > wnd.inf) {
                            var = wnd.sup;
                        }

                    }
                }
            } else if (pack.seq_num > wnd.inf) {
                if (wnd.sup >= wnd.inf) {
                    if (pack.seq_num > wnd.sup) {
                        wnd.sup = pack.seq_num;
                    }
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, addr);
                if (pack.seq_num == wnd.inf + 1) {
                    var = wnd.sup;
                    if (wnd.sup < wnd.inf) {
                        var = MAX_SEQ_NUM + wnd.sup;
                    }
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < var)) {
                        //scrivi su file(ACKED A 0) da wnd_buff THREAD
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
                        if (wnd.sup > wnd.inf) {
                            var = wnd.sup;
                        }
                    }
                }
            } else if ((pack.seq_num <= wnd.inf) && (pack.seq_num >= wnd.inf - N)) {
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, addr);
            }
        }*/
     }
}

//rilascio alla fine semaforo??
//SOSTITUIRE EXIT CON RETURN TODO
void list_function(int sockfd, int sem_stdout, struct sockaddr_in servaddr){
    int len, res;
    char **filename_list;
    struct ctrl_packet ack_pack;
    struct window wnd;
    struct packet pack;
    struct sembuf s_stdo, w_stdo;

    srand(time(0));

    memset((void*)&ack_pack, 0, sizeof(ack_pack));
    memset((void*)&pack, 0, sizeof(pack));
    memset((void*)&wnd, 0, sizeof(wnd));

    len = sizeof(servaddr);

    for(int n=0;n<N;n++){
        wnd.acked[n] = 0;
    }
    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    filename_list = malloc(sizeof(char *)*NUM_FILENAME_IN_PACK);
    if(filename == NULL) {
        err_handler(who, "malloc");
    }
    for(int n=0;n<NUM_FILENAME_IN_PACK;n++){
        filename_list[n] = (char *)malloc(MAX_FILENAME_SIZE);
    }

    ack_pack.cmd = 3;
    res = handshake_client(sockfd, &ack_pack, &servaddr, NULL);
    if(res == -1){
        err_handler(who, "handshake_client");
    }
    /*
     * ultimo ACK HANDSHAKE
     */
    ack_pack.seq_num = ack_pack.seq_num + 1;
    ack_pack.ack_num = ack_pack.ack_num;
    ack_pack.ack = 1;

    wnd.inf = ack_pack.ack_num;
    wnd.sup = ack_pack.ack_num;//TODO

    send_ctrl_packet(sockfd, ack_pack, servaddr);

    printf("\n\n-----LIST OF FILES-----\n\n")

    int num = 1;
    struct timespec rcv_timeout = {MAX_WAITING_TIME_SEC,0};
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&rcv_timeout, sizeof(rcv_timeout))<0){
        err_handler("List function", "setsockopt");
    }
    while(1) {
        while (recvfrom(sockfd, (void *) &pack, sizeof(pack), 0, (struct sockaddr *) &servaddr, &len) < 0) {
            if (errno != EINTR) {
                if (errno == EWOULDBLOCK) {
                    printf("\nServer stopped sending for too long. Closing connection\n");
                    if (start_close_connection(sockfd, wnd.inf + 1, servaddr) == 0) {
                        if (AUDIT == 1) {
                            printf("\nconnection closed correctly\n");
                        }
                        exit(0);
                    } else {
                        if (AUDIT == 1) {
                            printf("\nerror in close connection\n");
                        }
                        exit(-1);
                    }
                } else {
                    err_handler("List function", "recvfrom");
                }
            }
        }
        if (AUDIT == 1) {
            printf("\nricevuto pack: %d\n", pack.seq_num);
        }
        if (((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf) {
            if ((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)) {
                if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                ack_pack.ack_num = pack.seq_num;
                if (ADAPTIVE == 1) {
                    if (pack.TO == 1) {
                        ack_pack.TO = 1;
                        if (AUDIT_TO == 1) {
                            printf("\nACK TO packet %d", ack_pack.ack_num);
                        }
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                    }
                } else {
                    send_ctrl_packet(sockfd, ack_pack, servaddr);
                }
                if (pack.seq_num == wnd.inf + 1) {
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                        /*
                         * LEGGI PACCHETTO TODO
                         */
                        filename_list = wnd.wnd_buff[(wnd.inf + 1) % N].data;
                        for(int n=0;n<NUM_FILENAME_IN_PACK;n++){
                            printf("%d. %s\n", num, filename_list[n]);
                            num++;
                        }
                        if (wnd.wnd_buff[(wnd.inf + 1) % N].last == 1) {
                            printf("\n-----END OF LIST-----\n\n", path);
                            if(start_close_connection(sockfd, wnd.inf + 1, servaddr) == 0){
                                if(AUDIT == 1){
                                    printf("\nconnection closed correctly\n");
                                }
                                exit(0);
                            } else{
                                if(AUDIT == 1){
                                    printf("\nerror in close connection\n");
                                }
                                exit(-1);
                            }
                        }
                        wnd.acked[(wnd.inf + 1) % N] = 0;
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
                    }
                }
            } else if (pack.seq_num <= wnd.inf) {
                ack_pack.ack_num = pack.seq_num;
                if (ADAPTIVE == 1) {
                    if (pack.TO == 1) {
                        ack_pack.TO = 1;
                        if (AUDIT_TO == 1) {
                            printf("\n#SEQ<=INF; ACK TO packet %d", ack_pack.ack_num);
                        }
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, servaddr);
                    }
                }
            }
        }
        /*
         * MAX_SEQ_NUM TODO
         */
    }
}

/*
 * crea un file di prova.
 *
 * la dimensione del file è pari a circa D volte quella di un singolo pacchetto
 */
void write_file(){
    unsigned int D;
    char *filename;
    char buff[1024];
    char data[DATA_SIZE];
    int fd;
    int res;
    FILE *file;

    /*
     * D = 16846 -> 25MB FILE
     * D = 33693 -> 50MB FILE
     */
    D = 16846;

    int i = 0;
    for(int n=0;n<DATA_SIZE-1;n++){
        //data[n] = 'a';
        sprintf(&data[n], "%d", i);
        i = (i+1)%10;
    }
    data[DATA_SIZE - 1] = '\0';

    filename = (char *)malloc(MAX_FILENAME_SIZE);
    if(filename == NULL) {
        err_handler("Write", "malloc");
    }

    printf("\nEnter filename to create: ");

    getfrom_stdin(filename, "Enter filename to create: ", "Write", "scanf");
    sprintf(buff, "./client_files/%s", filename);
    while((fd = open(buff, O_RDWR|O_CREAT|O_TRUNC, 0666)) == -1){
        if(errno != EINTR){
            err_handler("Write", "open");
        }
    }
    file = fdopen(fd, "w+");
    if (file == NULL) {
        err_handler("Write", "fdopen");
    }
    for(int n=0;n<D;n++){
        res = fprintf(file, "%s", data);
        fflush(file);
    }
    fclose(file);
    close(fd);
    printf("\nfile %s created, DIM: %d X %d",filename, D, DATA_SIZE);
}
/*
 * CLIENT CREA SOCKET PER MANDARE RICHIESTA AL SERVER SU #PORTA E IP PRESTABILITI
 *
 * servaddr utilizzato successivamente nella funzione "send_packet" per mandare pacchetti al server
 * ( contiene #PORTA E IP del SERVER )
 */
void main(int argc, char *argv[]) {
    if (argc > 2) {
        printf("\nUsage: %s <server IP address>\n", argv[0]);
        exit(-1);
    }
    char ip_serv[IP_SIZE];
    if (argc < 2) {
        sprintf(ip_serv, "%s", IP_LOOPBACK);
        printf("\nUsing default Server IP address: %s (loopback).\n", ip_serv);
    } else {
        strncpy(ip_serv, argv[1], IP_SIZE);
        printf("\nUsing Server IP address: %s.\n", ip_serv);
    }
    char *cmd, who[7];
    int res, sockfd, sem_stdout;
    struct sockaddr_in servaddr;
    struct sembuf w_stdo, s_stdo;
    struct sigaction act;
    sigset_t set;
    pid_t pid;

    sprintf(who, "%s", "Client");
    cmd = (char *) malloc(CMD_SIZE);
    if (cmd == NULL) {
        err_handler(who, "malloc");
    }

    sem_stdout = semget(IPC_PRIVATE, 1, 0660);
    if (sem_stdout == -1) {
        err_handler(who, "semget");
    }
    res = semctl(sem_stdout, 0, SETVAL, 1);
    if (res == -1) {
        err_handler(who, "semctl");
    }
    w_stdo.sem_num = 0;
    w_stdo.sem_op = -1;
    w_stdo.sem_flg = 0;
    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    memset((void *) &servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERV_PORT);
    res = inet_pton(AF_INET, ip_serv, &servaddr.sin_addr);
    if (res <= 0) {
        err_handler(who, "inet_pton");
    }

    sigfillset(&set);
    act.sa_handler = event_handler;
    act.sa_mask = set;
    act.sa_flags = 0;
    sigaction(SIGUSR1, &act, NULL);
    while (1) {
        while (semop(sem_stdout, &w_stdo, 1) == -1) {
            if (errno != EINTR) {
                err_handler(who, "semop");
            }
        }
        printf("\nEnter a command: ");

        getfrom_stdin(cmd, "Enter a command: ", who, "scanf");

        if (strcmp("put", cmd) == 0) {
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd == -1) {
                err_handler(who, "socket");
            }

            pid = fork();
            if (pid == -1) {
                err_handler(who, "fork");
            } else if (pid == 0) {
                put_function(sockfd, sem_stdout, servaddr);
            }
        } else if (strcmp("get", cmd) == 0) {
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd == -1) {
                err_handler(who, "socket");
            }
            pid = fork();
            if (pid == -1) {
                err_handler(who, "fork");
            } else if (pid == 0){
                get_function(sockfd,sem_stdout, servaddr);
            }
        } else if (strcmp("list", cmd) == 0) {
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd == -1) {
                err_handler(who, "socket");
            }
            //TODO TOGLIERE FORK
            pid = fork();
            if (pid == -1) {
                err_handler(who, "fork");
            } else if (pid == 0){
                list_function(sockfd, sem_stdout, servaddr);
            }
            res = semop(sem_stdout, &s_stdo, 1);
            if(res == -1){
                err_handler(who, "semop");
            }
        } else if (strcmp("exit", cmd) == 0) {
            printf("\nClosing...\n");
            res = semop(sem_stdout, &s_stdo, 1);
            if(res == -1){
                err_handler(who, "semop");
            }
            exit(0);
        } else if (strcmp("write", cmd) == 0) {
            write_file();
            res = semop(sem_stdout, &s_stdo, 1);
            if(res == -1){
                err_handler(who, "semop");
            }
        } else if (strcmp("fill", cmd) == 0) {
            fill_directory();
            res = semop(sem_stdout, &s_stdo, 1);
            if(res == -1){
                err_handler(who, "semop");
            }
        } else {
            printf("\nInvalid command. Possible choices [put, get, list, exit] or [write, fill]\n");
            res = semop(sem_stdout, &s_stdo, 1);
            if (res == -1) {
                err_handler(who, "semop");
            }
        }
    }
    exit(0);
}