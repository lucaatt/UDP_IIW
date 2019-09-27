#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"
#include "thread_functions.h"

int handshake_server(int sockfd, struct ctrl_packet *ctrl_pack, struct packet *pack, struct sockaddr_in *addr) {
    struct ctrl_packet snd_pack;
    snd_pack.seq_num = 0;//rand();
    snd_pack.syn = 1;
    snd_pack.ack_num = ctrl_pack->seq_num;
    snd_pack.ack = 1;
    int len, res;
    //sleep(1);
    send_ctrl_packet(sockfd, snd_pack, *addr);

    len = sizeof(*addr);
    if (ctrl_pack->cmd == 1 || ctrl_pack->cmd == 2 || ctrl_pack->cmd == 3) {
        /*
         * METTERE TEMPO MASSIMO DI ATTESA
         *
         * IN OGNI CASO DOPO RECVFROM -> FILENAME RICEVUTO
         * SE ACK NON ARRIVA AL CLIENT VERRA RISPEDITO SENZA LEGGERE ANCORA FILENAME
         */
        res = recvfrom(sockfd, (void *) pack, sizeof(*pack), 0,
                       (struct sockaddr *) addr, &len);
        if (res < 0) {
            return -1;
        }
        if (pack->ack == 1) {
            if (pack->ack_num != snd_pack.seq_num) {
                return -1;
            }
            if (pack->seq_num != (ctrl_pack->seq_num) + 1) {
                return -1;
            }
            return 0;
        } else {
            return -1;
        }
    }
    return 0;
}

/*
 * Gestisce richiesta PUT (salva File dal client in server_files)
 */
void put_request_handler(int sockfd, struct packet pack, struct sockaddr_in addr) {
    int fd, len, res;
    FILE *file;
    char path[1520];
    char who[40];
    struct ctrl_packet ack_pack;
    struct window wnd;
    int var;
    int n;


    for (n = 0; n < N; n++) {
        wnd.acked[n] = 0;
    }
    /*
     * Finestra di ricezione
     *
     * INF: ultimo #SEQ letto in ordine con ACK spedito
     * SUP: più grande #SEQ ricevuto anche fuori ordine (letto o no)
     */
    wnd.inf = pack.seq_num;
    wnd.sup = pack.seq_num;

    len = sizeof(addr);
    printf(who, "%s", "Server put_request_handler");

    ack_pack.ack = 1;
    ack_pack.ack_num = pack.seq_num;
    if(ADAPTIVE == 1){
        if(pack.TO == 1){
            ack_pack.TO = 1;
            if(AUDIT_TO == 1){
                printf("\nACK TO packet %d", ack_pack.ack_num);
            }
        }
    }
    send_ctrl_packet(sockfd, ack_pack, addr);
    ack_pack.TO = 0;

    printf("\nServer saving file %s from Client...\n", pack.data);
    sprintf(path, "./server_files/%s", pack.data);
    while ((fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0660)) == -1) {
        if (errno != EINTR) {
            err_handler(who, "open");
        }
    }
    file = fdopen(fd, "w+");
    if (file == NULL) {
        err_handler(who, "fdopen");
    }
    struct timespec rcv_timeout = {MAX_WAITING_TIME_SEC,0};
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&rcv_timeout, sizeof(rcv_timeout))<0){
        err_handler("Put handler", "setsockopt");
    }
    while (1) {
        while(recvfrom(sockfd, (void *) &pack, sizeof(pack), 0,NULL,NULL) < 0) {//(struct sockaddr *) &addr, &len) < 0) {
            if(errno != EINTR){
                if(errno == EWOULDBLOCK){
                    printf("\nClient stopped sending for too long. Closing connection\n");
                    if(start_close_connection(sockfd, wnd.inf + 1, addr) == 0){
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
                    err_handler("Put handler", "recvfrom");
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
                        send_ctrl_packet(sockfd, ack_pack, addr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, addr);
                    }
                } else {
                    send_ctrl_packet(sockfd, ack_pack, addr);
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
                            printf("\nServer saved %s successfully\n", path);
                            if(start_close_connection(sockfd, wnd.inf + 1, addr) == 0){
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
                        send_ctrl_packet(sockfd, ack_pack, addr);
                        ack_pack.TO = 0;
                    } else {
                        send_ctrl_packet(sockfd, ack_pack, addr);
                    }
                }
                else{
                    send_ctrl_packet(sockfd, ack_pack, addr);
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

/*
 * GESTISCE RICHIESTA GET
 *
 * PACK ->  PACK.SEQ_NUM = #SEQ CLIENT( = 1)
 *          PACK.ACK_NUM = #SEQ SERVER( = 0) DA INCREMENTARE NEL PROSSIMO PACCHETTO
 * ADDR ->  #PORTA E IP DEL CLIENT CHE HA INVIATO LA RICHIESTA
 */

void get_request_handler(int sockfd, struct packet pack, struct sockaddr_in addr){
    /*
     * Gestione File
     */
    int actread;
    int fd, res;
    char path[1520], *filename, who[11];
    /*
    * Spedizione PACCHETTI
    */
    struct ctrl_packet ctrl_pack;
    //Usati per attesa Main Thread scorrimento finestra
    pthread_cond_t inf_cv;
    pthread_mutex_t inf_mux;
    pthread_cond_init(&inf_cv, NULL);
    pthread_mutex_init(&inf_mux, NULL);
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
    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;
    /*
     * Altro
     */
    int n;
    unsigned int temp;
    pthread_t tid;

    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&wnd, 0, sizeof(wnd));
    /*
     * Inizializzazione numero sequenza dopo Handshake
     */
    temp = pack.seq_num;
    pack.seq_num = pack.ack_num;
    pack.ack_num = temp;

    sprintf(who, "%s", "Server GET");
    sprintf(path, "./server_files/%s", pack.data);
    /*
     * MUTEX DEI TIMER
     * inizializzazione mutex -> lock prima di spedire pacchetto reativo al TIMER
     *                              unlock dopo START(send_thread o retrans_thread) e STOP(ack_thread) TIMER
     *                                                wnd_acked = 0                       wnd_acked = 1
     */
    for(int i=0; i<N; i++){
        res = pthread_mutex_init(&locks[i], NULL);
        if(res != 0){
            err_handler(who, "spinlock init");
        }
    }
    /*
     * INVIO PACK.ACK = 0 SE FILE NON ESISTE
     */
    while((fd = open(path, O_RDONLY)) == -1){
        if(errno != EINTR){
            //FileNotFound
            if(fnf_close_connection(sockfd, pack.seq_num +1, addr) == 0){
                if(AUDIT == 1){
                    printf("\nconnection closed correctly\n");
                }
            } else{
                if(AUDIT == 1){
                    printf("\nerror in close connection\n");
                }
            }
            printf("\nrequested file does not exist\n");
            exit(0);
        }
    }
    /*
     * RICEVUTO FILENAME VALIDO
     *
     * INIZIO S-R   PRIMO ACK INVIATO DAL CLIENT
     * INIZIALIZAZIONE FINESTRA SPEDIZIONE
     */
    wnd.inf = pack.seq_num;// INF -> ULTIMO #SEQ CON ACK RICEVUTO IN ORDINE
    wnd.sup = pack.seq_num + 1;// SUP -> ULTIMO #SEQ SERVER SPEDITO
    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }
    /*
     * TIMEOUT E RITRASMISSIONE
     */
    sig_to.sigev_notify = SIGEV_THREAD;
    sig_to.sigev_notify_function = &retransmission_thread;
    //Argomenti ack_thread
    args.sockfd = sockfd;
    args.wnd = &wnd;
    args.timers = timers;
    args.locks = locks;
    args.servaddr = &addr;
    args.send_time = &send_t;
    args.timeout = &timeout;
    args.to_mutexes = to_mux;
    args.to_seq_num = &to_snum;
    args.inf_cv = &inf_cv;
    args.inf_mux = &inf_mux;
    args.to_rwlock = &to_rwlock;
    //Argomenti retransmission_thread
    for(int i=0;i<N;i++){
        sig_to.sigev_value.sival_ptr = (void*)&rtx_args[i];
        rtx_args[i].timer_num = i;
        rtx_args[i].shared = &args;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
    }
    if(ADAPTIVE == 1){
        for(int i=0;i<2;i++){
            res = pthread_mutex_init(&to_mux[i], NULL);
            if(res == -1){
                err_handler(who, "mutex init");
            }
            pthread_mutex_lock(&to_mux[i]);
        }
    }
    pthread_rwlock_init(&to_rwlock, NULL);
    /*
     * START ACK_THREAD
     */
    res = pthread_create(&tid, NULL, ack_thread, (void*)&args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    /*
     * Invio primo pacchetto con dati e conferma esistenza file richiesto
     */
    pack.ack = 1;
    memset((void*)pack.data, 0, sizeof(DATA_SIZE));
    pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;
    res = read(fd, (void*)pack.data, DATA_SIZE - 1);
    if (res == -1) {
        if (errno != EINTR) {
            err_handler(who, "read");
        }
    }
    pack.data[res] = '\0';
    wnd.wnd_buff[pack.seq_num % N] = pack;
    if(ADAPTIVE == 1) {
        to_snum = pack.seq_num;
        pack.TO = 1;
        clock_gettime(CLOCK_MONOTONIC, &send_t);
        /*
         * Avverti ack_thread avvenuto invio pacchetto sample_rtt
         */
        pthread_mutex_unlock(&to_mux[1]);
        send_packet(sockfd, pack, addr);
        pack.TO = 0;
    }
    else if(ADAPTIVE == 0){
        send_packet(sockfd, pack, addr);
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
     * Spedizione FILE
     */
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
        send_packet(sockfd, pack, addr);
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
        send_packet(sockfd, pack, addr);
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
    pthread_join(tid, NULL);
    printf("\nServer FILE %s sent\n", path);
    exit(0);
}

void list_request_handler(int sockfd, struct packet pack, struct sockaddr_in addr){
    char *arr_nomi[NUM_FILENAME_IN_PACK];
    struct dirent **namelist;
    //Usati per attesa Main Thread scorrimento finestra
    pthread_cond_t inf_cv;
    pthread_mutex_t inf_mux;
    pthread_cond_init(&inf_cv, NULL);
    pthread_mutex_init(&inf_mux, NULL);
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
    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;
    /*
     * Altro
    */
    char who[40];
    int n, j, res;
    j=0;
    unsigned int temp;
    pthread_t tid;

    sprintf(who, "%s", "List handler");

    for ( int m = 0; m < NUM_FILENAME_IN_PACK; m++) {
        arr_nomi[m] = (char *) malloc(MAX_FILENAME_SIZE);
    }

    memset((void*)&wnd, 0, sizeof(wnd));
    /*
    * Inizializzazione numero sequenza dopo Handshake
    */
    temp = pack.seq_num;
    pack.seq_num = pack.ack_num;
    pack.ack_num = temp;
    wnd.inf = pack.seq_num;// INF -> ULTIMO #SEQ CON ACK RICEVUTO IN ORDINE
    wnd.sup = pack.seq_num + 1;// SUP -> ULTIMO #SEQ SERVER SPEDITO TODO
    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }
    /*
    * MUTEX DEI TIMER
    * inizializzazione mutex -> lock prima di spedire pacchetto reativo al TIMER
    *                              unlock dopo START(send_thread o retrans_thread) e STOP(ack_thread) TIMER
    *                                                wnd_acked = 0                       wnd_acked = 1
    */
    for(int i=0; i<N; i++){
        res = pthread_mutex_init(&locks[i], NULL);
        if(res != 0){
            err_handler(who, "spinlock init");
        }
    }
    /*
     * TIMEOUT E RITRASMISSIONE
     */
    sig_to.sigev_notify = SIGEV_THREAD;
    sig_to.sigev_notify_function = &retransmission_thread;
    //Argomenti ack_thread
    args.sockfd = sockfd;
    args.wnd = &wnd;
    args.timers = timers;
    args.locks = locks;
    args.servaddr = &addr;
    args.send_time = &send_t;
    args.timeout = &timeout;
    args.to_mutexes = to_mux;
    args.to_seq_num = &to_snum;
    args.inf_cv = &inf_cv;
    args.inf_mux = &inf_mux;
    args.to_rwlock = &to_rwlock;
    //Argomenti retransmission_thread
    for(int i=0;i<N;i++){
        sig_to.sigev_value.sival_ptr = (void*)&rtx_args[i];
        rtx_args[i].timer_num = i;
        rtx_args[i].shared = &args;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
    }
    if(ADAPTIVE == 1){
        for(int i=0;i<2;i++){
            res = pthread_mutex_init(&to_mux[i], NULL);
            if(res == -1){
                err_handler(who, "mutex init");
            }
            pthread_mutex_lock(&to_mux[i]);
        }
    }
    pthread_rwlock_init(&to_rwlock, NULL);

    n = scandir("./server_files/",&namelist, NULL,alphasort);
    if (n == -1) {
        perror("scandir");
        exit(0);
    }
    /*
     * START ACK_THREAD
     */
    res = pthread_create(&tid, NULL, ack_thread, (void*)&args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    while(n>2){
        sprintf(&pack.data[j*MAX_FILENAME_SIZE], "%s", namelist[n-1]->d_name);
        //sprintf(arr_nomi[j], "%s", namelist[n]);
        j++; //indice dell'array di nomi arr_nomi
        n--; //indice della struct di nomi namelist
        if(j == NUM_FILENAME_IN_PACK - 1){
            //pack.data = arr_nomi;
            pack.seq_num = (pack.seq_num + 1)%MAX_SEQ_NUM;

            while(pack.seq_num > wnd.inf + N){
                pthread_mutex_lock(&inf_mux);
                pthread_cond_wait(&inf_cv, &inf_mux);
                pthread_mutex_unlock(&inf_mux);
            }
            res = pthread_mutex_lock(&locks[pack.seq_num % N]);
            if(res != 0){
                err_handler("send thread", "spin_lock");
            }
            //Array di pacchetti spediti senza ACK
            //In caso di TIMEOUT ritrasmessi da RETRANSMISSION_THREAD
            wnd.wnd_buff[pack.seq_num % N] = pack;
            wnd.acked[pack.seq_num % N] = 0;
            wnd.sup = pack.seq_num;

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
            send_packet(sockfd, pack, addr);
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


            j=0; // azzera il riempimento del pacchetto
        }
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
        send_packet(sockfd, pack, addr);
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
    pthread_join(tid, NULL);
    printf("\nServer Filename List sent\n");
    close(sockfd);//TODO
    exit(0);
}

/*
void list_request_handler(int sockfd, struct packet pack, struct sockaddr_in addr) {
    int actread;
    int fd, res;
    char path[1520], who[11];
    struct ctrl_packet ctrl_pack;
    struct window wnd;
    struct dirent **namelist;
    int n, k, j = 0;
    char *arr_nomi[5];
    unsigned int temp;
    pthread_t tid[2];

    struct send_thread_args send_args;
    struct ack_thread_args args[N];
    struct sigevent sig_to;
    pthread_spinlock_t locks[N];

    temp = pack.seq_num;
    pack.seq_num = pack.ack_num;
    pack.ack_num = temp;

    memset((void *) &ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void *) &wnd, 0, sizeof(wnd));
    sprintf(who, "%s", "Server LIST");

    for ( int m = 0; m < 5; m++) {
        arr_nomi[m] = (char *) malloc(MAX_FILENAME_SIZE);
    }

    for (int i = 0; i < N; i++) {
        res = pthread_spin_init(&locks[i], PTHREAD_PROCESS_PRIVATE);
        if (res != 0) {
            err_handler(who, "spinlock init");
        }
    }
    k = scandir("./server_files/",&namelist, NULL,alphasort);
    if (n == -1) {
        perror("scandir");
        exit(0);
    }

    for(int i;i<READY_SIZE;i++){
        send_args.slots[i] = 0;
    }

    res = pthread_create(&tid[1], NULL, send_thread, (void*)&send_args);
    if(res == -1) {
        err_handler(who, "pthread_create");
    }

    int slot = 0;

    while(k>0){
        sprintf(arr_nomi[j], "%s", namelist[k]);
        j++; //indice dell'array di nomi arr_nomi
        k--; //indice della struct di nomi namelist
            if(j==4){
                pack.data = arr_nomi;
                pack.seq_num = (pack.seq_num + 1)%MAX_SEQ_NUM;

                while(send_args.slots[slot] == 1);
                send_args.ready[slot] = pack;
                send_args.slots[slot] = 1;
                slot = (slot + 1)%READY_SIZE;

                memset((void*)pack.data, 0, DATA_SIZE);

                j=0; // azzera il riempimento del pacchetto
            }
        }
    }
*/

int main(int argc, char *argv[]) {
    int listen_sockfd, connection_sockfd;
    int res, len, cmd;
    struct sockaddr_in addr;
    struct packet pack;
    struct ctrl_packet ctrl_pack;
    char who[7];
    pid_t pid;

    sprintf(who, "%s", "Server");
    memset((void *) &ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void *) &pack, 0, sizeof(pack));
    memset((void *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERV_PORT);

    printf("Server running...\n\n");

    if ((listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        err_handler(who, "socket");
    }
    if (bind(listen_sockfd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        err_handler(who, "bind");
    }
    len = sizeof(addr);

    while (1) {
        printf("\nWaiting for a request\n");
        /*
         * IN ATTESA DI RICHIESTA DA QUALCHE CLIENT
         *
         * ctrl_pack -> conterra CMD e #SEQ Client = 0
         * addr      -> conterra #PORTA e IP del CLIENT
         *              (UTILIZZARE NELLA FUNZIONE "SEND_PACKET")
         */
        res = recvfrom(listen_sockfd, (void *) &ctrl_pack, sizeof(ctrl_pack), 0,
                       (struct sockaddr *) &addr, &len);
        if (res < 0) {
            err_handler(who, "recvfrom");
        }

        cmd = ctrl_pack.cmd;
        /*
         * CREAZIONE NUOVA SOCKET CHE VERRA UTILIZZATA DA QUI IN POI PER QUESTO CLIENT
         * L'ALTRA SOCKET RIMARRA IN ATTESA DI ALTRE RICHIESTE (SERVITE IN CONCORRENZA)
         */
        if ((connection_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            err_handler(who, "socket");
        }

        /*
         * ctrl_pack  -> #SEQ client usato per mandare ACK
         *
         * DOPO HANDSHAKE PACK ->  pack.seq_num = #SEQ client = 1
         *                            pack.ack_num = #SEQ SERVER (ack ricevuto)
         *                         pack.data = filename (se GET o PUT)
         */
        pid = fork();
        if (pid == -1) {
            err_handler(who, "fork");
        }
        if (pid == 0) {
            srand(time(0));//NECESSARIO PER GENERARE OGNI VOLTA SEQUENZE DIVERSE DI NUMERI PSEUDORANDOM
            if (cmd == 1) {
                res = handshake_server(connection_sockfd, &ctrl_pack, &pack, &addr);
                if (res == -1) {
                    err_handler(who, "handshake_server");
                }
                put_request_handler(connection_sockfd, pack, addr);
            }
            else if (cmd == 2) {
                res = handshake_server(connection_sockfd, &ctrl_pack, &pack, &addr);
                if (res == -1) {
                    err_handler(who, "handshake_server");
                }
                get_request_handler(connection_sockfd, pack, addr);
            }
            else if (cmd == 3) {
                res = handshake_server(connection_sockfd, &ctrl_pack, &pack, &addr);
                if (res == -1) {
                    err_handler(who, "handshake_server");
                }
                list_request_handler(connection_sockfd, pack, addr);
            }
            else{
                printf("\nrequest from client not recognized\n");
                exit(0);
            }
            //request_handler(connection_sockfd, pack, cmd, addr);

        }


    }
}

