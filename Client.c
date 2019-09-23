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
int handshake_client(int sockfd, struct ctrl_packet *ctrl_pack, struct sockaddr_in *servaddr) {
    struct ctrl_packet rcv_pack;
    ctrl_pack->seq_num = 0;//rand();
    ctrl_pack->syn = 1;
    int len, res;
    struct timespec connection_timeout = {2,0};
    int attempts = 0;

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
                send_ctrl_packet(sockfd, *ctrl_pack, *servaddr);
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

/*
void get_function(int sockfd, int sem_stdout, struct sockaddr_in servaddr) {
    int fd, len, res;
    FILE *file;
    char path[1520], *filename;
    char who[40];
    struct ctrl_packet ack_pack;
    struct packet pack;
    struct window wnd;
    int var;
    int n;
    struct sembuf s_stdo, w_stdo;

    //wnd.inf = pack.seq_num;
    //wnd.sup = pack.seq_num;

    filename = (char *)malloc(MAX_FILENAME_SIZE);
    if(filename == NULL) {
        err_handler(who, "malloc");
    }

    memset((void *) &pack, 0, sizeof(pack));


    res = handshake_client(sockfd, &ack_pack, &servaddr);
    if (res == -1) {
        err_handler(who, "handshake_client");
    }

    pack.seq_num = 1;
    pack.ack_num = 0;

    w_stdo.sem_num = 0;
    w_stdo.sem_op = -1;
    w_stdo.sem_flg = 0;
    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    len = sizeof(servaddr);
    printf(who, "%s", "Client GET\n");


    printf("\nEnter filename to receive: ");

    getfrom_stdin(filename, "Enter filename to receive: ", who, "scanf");
    res = semop(sem_stdout, &s_stdo, 1);
    if (res == -1) {
        err_handler(who, "semop");
    }
    res = sprintf(pack.data, "%s", filename);
    if (res < 0) {
        err_handler(who, "sprintf");
    }
    printf("\nfilename in data %s\n",filename);

    send_packet(sockfd, pack, servaddr);
    ack_pack.ack = 1;
    ack_pack.ack_num = pack.seq_num;
    //printf("ACK: %d\n", ack_pack.ack_num);
    //send_ctrl_packet(sockfd, ack_pack, servaddr);

    //printf("Client sta ricevendo il file %s dal Server", filename);
    //sprintf(path, "./client_files/%s", filename);
    /*while ((fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0660)) == -1) {
        if (errno != EINTR) {
            err_handler(who, "open");
        }
    }
    file = fdopen(fd, "w+");
    if (file == NULL) {
        err_handler(who, "fdopen");
    }
    while (1) {

        memset((void *) pack.data, 0, sizeof(DATA_SIZE));
        res = recvfrom(sockfd, (void *) &pack, sizeof(pack), 0, (
                struct sockaddr *) &servaddr, &len);
        printf("\nricevuto pack: %d\n", pack.seq_num);
        if (res < 0) {
            err_handler(who, "recvfrom");
        }

        if (((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf) {
            if ((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)) {
                if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;
                wnd.acked[pack.seq_num % N] = 1;
                ack_pack.ack_num = pack.seq_num;
                sleep(1);
                printf("\nACK: %d\n", pack.seq_num);
                send_ctrl_packet(sockfd, ack_pack, servaddr);
                if (pack.seq_num == wnd.inf + 1) {
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                        res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf + 1) % N].data);
                        printf("\nscritto: %d\n", wnd.inf + 1);
                        if (res < 0) {
                            err_handler(who, "fprintf");
                        }
                        fflush(file);
                        if (pack.last == 1) {
                            printf("\nClient ha salvato con successo: %s \n", path);
                            exit(0);
                        }
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
                    }
                }
            } else if (pack.seq_num <= wnd.inf) {
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, servaddr);
            }
        } else if (((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf) {
            if (pack.seq_num < ((wnd.inf + N) % MAX_SEQ_NUM)) {
                if (wnd.sup >= wnd.inf) {
                    wnd.sup = pack.seq_num;
                } else if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;
                wnd.acked[pack.seq_num % N] = 1;
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, servaddr);
                if (pack.seq_num == ((wnd.inf + 1) % N)) {
                    var = wnd.sup;
                    if (wnd.sup < wnd.inf) {
                        var = MAX_SEQ_NUM + wnd.sup;
                    }
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < var)) {
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
                wnd.wnd_buff[pack.seq_num % N] = pack;
                wnd.acked[pack.seq_num % N] = 1;
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, servaddr);
                if (pack.seq_num == wnd.inf + 1) {
                    var = wnd.sup;
                    if (wnd.sup < wnd.inf) {
                        var = MAX_SEQ_NUM + wnd.sup;
                    }
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;
                        if (wnd.sup > wnd.inf) {
                            var = wnd.sup;
                        }
                    }
                }
            } else if ((pack.seq_num <= wnd.inf) && (pack.seq_num >= wnd.inf - N)) {
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, servaddr);
            }
        }
    }
}
*/
/*
void close_server(int sockfd, struct sockaddr_in servaddr){
    struct packet pack;
    struct ctrl_packet ctrl_pack;
    int res;
    char who[11];

    memset((void *) &ctrl_pack, 0, sizeof(ctrl_pack));


    ctrl_pack.cmd = 4;

    res = handshake_client(sockfd, &ctrl_pack, &servaddr);
    if (res == -1) {
        err_handler(who, "handshake_client");
    }
}
*/

void put_function(int sockfd, int sem_stdout, struct sockaddr_in servaddr){
    int actread;
    int fd, res;
    char buff[1024], *filename, who[11];
    struct packet pack;
    struct sembuf s_stdo, w_stdo;
    struct ctrl_packet ctrl_pack;
    struct window wnd;
    pid_t ppid;
    int n;


    struct send_thread_args send_args;
    struct ack_thread_args args[N];
    struct sigevent sig_to;
    pthread_spinlock_t locks[N];

    struct itimerspec timeout;

    timeout.it_value.tv_sec = DEF_TO_SEC;
    timeout.it_value.tv_nsec = DEF_TO_NSEC;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;

    w_stdo.sem_num = 0;
    w_stdo.sem_op = -1;
    w_stdo.sem_flg = 0;
    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

    srand(time(0));//NECESSARIO PER GENERARE OGNI VOLTA SEQUENZE DIVERSE DI NUMERI PSEUDORANDOM


    ppid = getppid();
    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&wnd, 0, sizeof(wnd));
    memset((void*)&pack, 0, sizeof(pack));

    sprintf(who, "%s", "Client PUT");
    ctrl_pack.cmd = 1;


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
    /*
* SPINLOCK DEI TIMER
* inizializzazione spinlock -> lock prima di spedire pacchetto reativo al TIMER
*                              unlock dopo START(send_thread o retrans_thread) e STOP(ack_thread) TIMER
*                                                wnd_acked = 0                       wnd_acked = 1
*/

    for(int i=0; i<N; i++){
        res = pthread_spin_init(&locks[i], PTHREAD_PROCESS_PRIVATE);
        if(res != 0){
            err_handler(who, "spinlock init");
        }
    }


    res = sprintf(pack.data, "%s", filename);
    if(res < 0){
        err_handler(who, "sprintf");
    }

    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }

    pthread_t tid[2];

    sig_to.sigev_notify = SIGEV_THREAD;
    sig_to.sigev_notify_function = &retransmission_thread;
    timer_t timers[N];
    send_args.timers = timers;
    send_args.locks = locks;
    for(int i=0;i<N;i++){
        sig_to.sigev_value.sival_ptr = (void*)&args[i];
        args[i].sockfd = sockfd;
        args[i].wnd = &wnd;
        args[i].timer_num = i;
        args[i].timers = timers;
        args[i].locks = locks;
        args[i].servaddr = servaddr;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
    }

    res = handshake_client(sockfd, &ctrl_pack, &servaddr);
    if(res == -1){
        err_handler(who, "handshake_client");
    }

/*
    memset((void*)&pack, 0, sizeof(pack));
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

    for(int i=0; i<N; i++){
        res = pthread_spin_init(&locks[i], PTHREAD_PROCESS_PRIVATE);
        if(res != 0){
            err_handler(who, "spinlock init");
        }
    }


    res = sprintf(pack.data, "%s", filename);
    if(res < 0){
        err_handler(who, "sprintf");
    }
  */
    pack.seq_num = ctrl_pack.seq_num + 1;
    pack.ack_num = ctrl_pack.ack_num;
    pack.ack = 1;

    wnd.inf = pack.seq_num - 1;
    wnd.sup = pack.seq_num;

    for(int i=0;i<N;i++){
        args[i].servaddr = servaddr;
    }

    res = pthread_create(&tid[0], NULL, ack_thread, (void*)&args[0]);
    if(res == -1){
        err_handler(who, "pthread_create");
    }

    wnd.wnd_buff[pack.seq_num % N] = pack;
    wnd.acked[pack.seq_num % N] = 0;
    send_packet(sockfd, pack, servaddr);

    res = timer_settime(timers[pack.seq_num % N], 0, &timeout, NULL);
    if(res == -1){
        err_handler("send thread", "settime");
    }

    while((wnd.sup - wnd.inf) != 0);

    sprintf(buff, "./client_files/%s", filename);
    while((fd = open(buff, O_RDONLY)) == -1){
        if(errno != EINTR){
            err_handler(who, "open");
        }
    }
    actread = 0;
    pack.ack = 0;
    memset((void*)pack.data, 0, sizeof(DATA_SIZE));

    send_args.sockfd = sockfd;
    send_args.wnd = &wnd;
    send_args.servaddr = servaddr;

    for(int i;i<READY_SIZE;i++){
        send_args.slots[i] = 0;
    }
    res = pthread_create(&tid[1], NULL, send_thread, (void*)&send_args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    int slot = 0;
    while((res = read(fd, (void*)&(pack.data[actread]), 1)) != 0){
        if(res == -1){
            if(errno != EINTR){
                err_handler(who, "read");
            }
        }
        actread++;
        actread = actread % DATA_SIZE;
        if(actread == DATA_SIZE - 1){
            pack.data[actread] = '\0';
            actread++;
            actread = actread % DATA_SIZE;
            pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;

            while(send_args.slots[slot] == 1);
            send_args.ready[slot] = pack;
            send_args.slots[slot] = 1;
            slot = (slot + 1)%READY_SIZE;

            memset((void*)pack.data, 0, DATA_SIZE);
        }
    }
    pack.last = 1;
    pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;

    while(send_args.slots[slot] == 1);
    send_args.ready[slot] = pack;
    send_args.slots[slot] = 1;

    for(int i=0;i<2;i++){
        pthread_join(tid[i], NULL);
    }
    printf("\nClient FILE %s sent\n", filename);///ASPETTA CONFERMA DA SERVER(manda ACK)
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
    struct timespec start_time = {0,0};
    struct timespec end_time = {0, 0};
    struct timespec sample_RTT = {0, 0};

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
    res = handshake_client(sockfd, &ack_pack, &servaddr);
    if(res == -1){
        err_handler(who, "handshake_client");
    }
/*
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
*/
    res = sprintf(pack.data, "%s", filename);///ULTIMO PACCHETTO HANDSHAKE CHE OLTRE ACK CONTIENE FILENAME
    if(res < 0){
        err_handler(who, "sprintf");
    }
    pack.seq_num = ack_pack.seq_num + 1;//#SEQ + 1 = 1   ultimo pacchetto mandato in handshake(ack_pack) [#SEQ = 0]
    pack.ack_num = ack_pack.ack_num;// #ACK relativo a #SEQ pacchetto SERVER in attesa dell' ACK
    pack.ack = 1;                 ///indica che pacchetto contiene ACK

    if(ADAPTIVE == 1){


        if(clock_gettime(CLOCK_MONOTONIC_RAW,&start_time) == -1)
            err_handler("get function","clock gettime");
        //prendo il tempo iniziale all'invio del pack
    }

    send_packet(sockfd, pack, servaddr);////////GESTIRE PERDITA


 /*
 * ACK DAL SERVER E CONFERMA ESISTENZA FILE
 *
 */
    struct timespec connection_timeout = {DEF_TO_SEC,0};
    int attempts = 0;
    len = sizeof(servaddr);
    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler(who, "setsockopt");
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
    if(clock_gettime(CLOCK_MONOTONIC_RAW,&end_time) == -1)
        err_handler(who,"clock gettime");

    if(ADAPTIVE == 1){


        //prendo il tempo finale alla ricezione dell'ack

        double sec = difftime(end_time.tv_sec,start_time.tv_sec);
        //long old_estimated = estimated;
        long nsec = end_time.tv_nsec - start_time.tv_nsec;
        long secn = sec*(1.0e9);

        long time_tot = secn + nsec;


        if(time_tot >(3*(1.0e6)) || time_tot<(1*(1.0e6))){
            SAMPLE_RTT_SEC = DEF_TO_SEC;
            SAMPLE_RTT_NSEC = DEF_TO_NSEC;
        }else{
            SAMPLE_RTT_SEC = time_tot/(1.0e9);
            if(SAMPLE_RTT_SEC > 0){
                SAMPLE_RTT_NSEC = time_tot - (SAMPLE_RTT_SEC*(1.0e9));
            }
            else{
                SAMPLE_RTT_NSEC = time_tot;
            }
        }

/*
        double alpha = 0.125;
        double beta = 0.250;

        estimated = ((1-alpha)*(old_estimated)) + ((alpha)*(time_tot));
        deviance = ((1-beta)*(deviance)) + (beta*(labs(time_tot - estimated)));
        time_tot = estimated + (4*deviance);

*/
        ///sample_RTT.tv_sec = sec;
        ////sample_RTT.tv_nsec = nsec;

        //connection_timeout = sample_RTT;
        connection_timeout.tv_sec = SAMPLE_RTT_SEC;
        connection_timeout.tv_nsec = SAMPLE_RTT_NSEC;
        printf("\n SAMPLE_RTT_SEC  %ld\n\n",SAMPLE_RTT_SEC);
        printf("\n SAMPLE_RTT_NSEC  %ld\n\n",SAMPLE_RTT_NSEC);



    }else{

        connection_timeout.tv_sec = 0;
        connection_timeout.tv_nsec = 0;
    }


    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&connection_timeout, sizeof(connection_timeout))<0){
        err_handler("Handshake", "setsockopt");
    }

      /*
    len = sizeof(servaddr);
    res = recvfrom(sockfd, (void *)&pack, sizeof(pack), 0, (struct sockaddr *)&servaddr, &len);
    if (res < 0) {
        exit(-1);
    }
       */
    if (pack.ack == 1) {
        if (pack.ack_num != ack_pack.seq_num + 1) {
            printf("\nwrong ACK number from SERVER\n");
            kill(ppid, SIGUSR1);
            exit(-1);
        }
        if(pack.seq_num != ack_pack.ack_num + 1){
            printf("\nwrong SEQ number from SERVER\n");
            kill(ppid, SIGUSR1);
            exit(-1);
        }
    }
    else if(pack.ack == 0){
        printf("\nMessage from SERVER: FILE %s does not exist\n", filename);
        kill(ppid, SIGUSR1);
        exit(0);
    }
    /*
     * SE FILE ESISTE PACK CONTERRA GIA PRIMO PEZZO FILE
     */
    ack_pack.ack_num = pack.seq_num;
    ack_pack.ack = 1;//////////////////DA QUI IN POI ACK_PACK USATO SOLO PER ACK
    printf("\ninviato ACK: %d\n", pack.seq_num);//todo
    send_ctrl_packet(sockfd, ack_pack, servaddr);

    /*
     * FINESTRA RICEZIONE INIZIALIZZAZZIONE
     */
    wnd.sup = pack.seq_num;//SUP -> #SEQ (entro limiti finestra) MAGGIORE TRA I PACCHETTI RICEVUTI(anche fuori ordine)
    wnd.inf = pack.seq_num;//INF -> ULTIMO PACCHETTO RICEVUTO IN ORDINE E LETTO
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
    res = fprintf(file, "%s", pack.data);
    if(res < 0){
        err_handler(who, "fprintf");
    }
    fflush(file);
    /*
     * RICEZIONE, SCRITTURA E ACK RESTO DEL FILE DAL SERVER
     *
     */
     while(1){
         memset((void*)pack.data, 0, sizeof(DATA_SIZE));
         res = recvfrom(sockfd, (void*)&pack, sizeof(pack), 0,
                        (struct sockaddr *)&servaddr, &len);
         if(AUDIT == 1) {
             printf("\nricevuto pack: %d\n", pack.seq_num);
         }
         if(res < 0){
             err_handler(who, "recvfrom");
         }
         if(((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf){
             if((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)){
                 if(pack.seq_num > wnd.sup){
                     wnd.sup = pack.seq_num;
                 }
                 wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                 wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                 ack_pack.ack_num = pack.seq_num;
                 sleep(1);//todo
                 send_ctrl_packet(sockfd, ack_pack, servaddr);
                 if(pack.seq_num == wnd.inf + 1){
                     while((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)){
                         res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf+1)%N].data);
                         if(AUDIT == 1) {
                             printf("\nscritto: %d\n", wnd.inf + 1);
                         }
                         if(res < 0){
                             err_handler(who, "fprintf");
                         }
                         fflush(file);
                         if(wnd.wnd_buff[(wnd.inf+1)%N].last == 1){
                             printf("\nClient saved %s successfully\n", path);
                             kill(ppid,SIGUSR1);
                             ack_pack.ack = 0;
                             ack_pack.fin = 1;
                             send_ctrl_packet(sockfd, ack_pack, servaddr);//ASPETTA CONFERMA CHIUSURA
                             //exit(0);
                             /*
                              * EXIT VA TOLTO PER PERMETTERE DI INVIARE GLI ACK PERSI
                              * ANCHE SE FILE SALVATO CORRETTAMENTE
                              */
                         }
                         wnd.acked[(wnd.inf + 1) % N] = 0;//--------------------------------------------
                         wnd.inf = (wnd.inf + 1)%MAX_SEQ_NUM;

                     }
                 }
             }
             else if(pack.seq_num <= wnd.inf){
                 ack_pack.ack_num = pack.seq_num;
                 send_ctrl_packet(sockfd, ack_pack, servaddr);
             }
         }
         else if(((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf){ //todo per limite del file;
             if(pack.seq_num < ((wnd.inf + N)%MAX_SEQ_NUM)){
                 if(wnd.sup >= wnd.inf) {
                     wnd.sup = pack.seq_num;
                 }
                 else if(pack.seq_num > wnd.sup){
                     wnd.sup = pack.seq_num;
                 }
                 wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                 wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                 ack_pack.ack_num = pack.seq_num;
                 send_ctrl_packet(sockfd, ack_pack, servaddr);
                 if(pack.seq_num == ((wnd.inf + 1)%MAX_SEQ_NUM)){
                     var = wnd.sup;
                     if(wnd.sup < wnd.inf){
                         var = MAX_SEQ_NUM + wnd.sup;
                     }
                     while((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < var)){
                         //scrivi su file(ACKED A 0) da wnd_buff THREAD
                         wnd.inf = (wnd.inf + 1)%MAX_SEQ_NUM;
                         if(wnd.sup > wnd.inf){
                             var = wnd.sup;
                         }

                     }
                 }
             }
             else if(pack.seq_num > wnd.inf){
                 if(wnd.sup >= wnd.inf) {
                     if(pack.seq_num > wnd.sup) {
                         wnd.sup = pack.seq_num;
                     }
                 }
                 wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                 wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                 ack_pack.ack_num = pack.seq_num;
                 send_ctrl_packet(sockfd, ack_pack, servaddr);
                 if(pack.seq_num == wnd.inf + 1){
                     var = wnd.sup;
                     if(wnd.sup < wnd.inf){
                         var = MAX_SEQ_NUM + wnd.sup;
                     }
                     while((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < var)){
                         //scrivi su file(ACKED A 0) da wnd_buff THREAD
                         wnd.inf = (wnd.inf + 1)%MAX_SEQ_NUM;
                         if(wnd.sup > wnd.inf){
                             var = wnd.sup;
                         }
                     }
                 }
             }
             else if((pack.seq_num <= wnd.inf) && (pack.seq_num >= wnd.inf - N)){

                 //IF FIN = 1 CHIUSURA
                 ack_pack.ack_num = pack.seq_num;
                 send_ctrl_packet(sockfd, ack_pack, servaddr);
             }
         }
     }
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
        } else if (strcmp("exit", cmd) == 0) {
            printf("\nClosing...\n");
            res = semop(sem_stdout, &s_stdo, 1);
            if(res == -1){
                err_handler(who, "semop");
            }
            exit(0);
        } else {
            printf("\nInvalid command. Possible choices [put, get, list, exit]\n");
            res = semop(sem_stdout, &s_stdo, 1);
            if (res == -1) {
                err_handler(who, "semop");
            }
        }
    }
    exit(0);
}