#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"

#define IP_LOOPBACK "127.0.0.1"
#define IP_SIZE 16

void event_handler() {
    printf("\n");
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

    printf("\nFUNZIONA\n");
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
                    timer_settime(args->timers[ack_pack.ack_num % N], 0, &stop_to, NULL);
                    wnd->acked[ack_pack.ack_num % N] = 1;
                    printf("\nACK: %d\n", ack_pack.ack_num);//todo
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

/*
 * Serve a stabilire una connessione con il SERVER in ascolto su un #PORTA predefinito
 * che ricevuto il primo mess dal CLIENT genera una nuova SOCKET con diverso #PORTA per
 * gestire più richieste in concorrenza.
 * HANDSHAKE necessario per ottenere nuova PORTA del SERVER, inizializzare #SEQUENZA ecc
*/
int handshake_client(int sockfd, struct ctrl_packet *ctrl_pack, struct sockaddr_in *servaddr) {
    struct ctrl_packet rcv_pack;
    ctrl_pack->seq_num = 0;//rand();
    ctrl_pack->syn = 1;
    int len, res;

    //ifdef AUDIT
    // printf("\n\nclient #seq: %d\n", ctrl_pack->seq_num);

    send_ctrl_packet(sockfd, *ctrl_pack, *servaddr);

    //CONTROLLO PORTA CLIENT
    /*
    struct sockaddr_in addr;
    memset((void*)&addr, 0, sizeof(addr));
    socklen_t len1 = sizeof(addr);
    if(getsockname(sockfd, (struct sockaddr *)&addr, &len1) == -1){
        printf("\nerr getsock\n");
        exit(-1);
    }
    printf("\nport %d\n", ntohs(addr.sin_port));
    */

    len = sizeof(*servaddr);

    /*
     * Dopo aver ricevuto correttamente il pacchetto dal SERVER "servaddr" conterrà il
     * nuovo #PORTA che verrà utilizzato dal CLIENT per inviere i mess successivi
    */
    res = recvfrom(sockfd, (void *) &rcv_pack, sizeof(rcv_pack), 0,
                   (struct sockaddr *) servaddr, &len);//////////if res < size pack non mandare ack
    if (res < 0) {
        return -1;
    }
    //ifdef AUDIT
    //printf("\nNUOVA PORTA SERVER: %d\n", ntohs(servaddr->sin_port));
    //printf("\nserver #seq: %d\nACK dal server: %d\n", rcv_pack.seq_num, rcv_pack.ack_num);

    //Controllo ACK dal SERVER in rcv_pack
    if (rcv_pack.ack == 1) {
        if (rcv_pack.ack_num != ctrl_pack->seq_num) {
            return -1;
        } else {
            ctrl_pack->ack_num = rcv_pack.seq_num;
        }
    } else {
        return -1;
    }
    if (ctrl_pack->cmd == 1) {
        /*
         * ctrl pack ha #SEQ e #ACK(seq server)
         * servaddr ha nuovo #PORT del server
         * se PUT ultimo ACK handshake in piggyback con FILENAME
         */
        ctrl_pack->syn = 0;
        return 0;
    }
    //se comando diverso da put terminare handshake con ACK("senza dati")[
    // --> FORSE NOME FILE SE GET??[quindi uguale a PUT]
    return 0;
}


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
    }*/
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

void * send_thread(void * arg){
    int slot = 0;
    int res;
    struct send_thread_args *args = (struct send_thread_args *)arg;
    struct itimerspec timeout;

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
        args->wnd->wnd_buff[args->ready[slot].seq_num % N] = args->ready[slot];
        args->wnd->acked[args->ready[slot].seq_num % N] = 0;
        args->wnd->sup = args->ready[slot].seq_num;
        send_packet(args->sockfd, args->ready[slot], args->servaddr);///SCARTA CON PROBABILITA p-------------------

        ///////////////////////////////////SET TIMER
        res = timer_settime(args->timers[args->ready[slot].seq_num % N], 0, &timeout, NULL);
        if(res == -1){
            err_handler("send thread", "settime");
        }
        printf("\ninviato pack: %d\n", args->ready[slot].seq_num);//todo

        args->slots[slot] = 0;
        if(args->ready[slot].last == 1) {
            printf("\nsend_thread exit\n");
            pthread_exit(0);
        }
        slot = (slot + 1) % READY_SIZE;
    }
}

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



    ppid = getppid();
    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&wnd, 0, sizeof(wnd));////-----------------------------------------------

    sprintf(who, "%s", "Client PUT");
    ctrl_pack.cmd = 1;

    res = handshake_client(sockfd, &ctrl_pack, &servaddr);
    if(res == -1){
        err_handler(who, "handshake_client");
    }

    w_stdo.sem_num = 0;
    w_stdo.sem_op = -1;
    w_stdo.sem_flg = 0;
    s_stdo.sem_num = 0;
    s_stdo.sem_op = 1;
    s_stdo.sem_flg = 0;

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

    res = sprintf(pack.data, "%s", filename);
    if(res < 0){
        err_handler(who, "sprintf");
    }
    pack.seq_num = ctrl_pack.seq_num + 1;// % MAX_SEQ_NUM
    pack.ack_num = ctrl_pack.ack_num;
    pack.ack = 1;

    //thread per ricezione ACK e gestione finestra MITTENTE
    //array di timer e interruzione se ACK ricevuto
    //TIME OUT -> SIGALARM HANDLER

    wnd.inf = pack.seq_num - 1;
    wnd.sup = pack.seq_num;
    for(n=0;n<N;n++){///******---------------------------------------------------------
        wnd.acked[n] = 0;
    }
    //wnd.wnd_buff[pack.seq_num % N] = pack;//timer--------------------------------------------------------

    pthread_t tid[2];////----------------------------------------------------------------------

    //args.sockfd = sockfd;
    //args.wnd = &wnd;

    sig_to.sigev_notify = SIGEV_THREAD;
    sig_to.sigev_notify_function = &retransmission_thread;
    //sig_to.sigev_notify_function = &ack_thread;
    //sig_to.sigev_value.sival_ptr = (void*)&args;
    timer_t timers[N];
    send_args.timers = timers;
    //args.timers = timers;
    for(int i=0;i<N;i++){
        //sig_to.sigev_value.sival_int = i;
        sig_to.sigev_value.sival_ptr = (void*)&args[i];
        args[i].sockfd = sockfd;
        args[i].wnd = &wnd;
        args[i].timer_num = i;
        args[i].timers = timers;
        args[i].servaddr = servaddr;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
        //send_args.timers[i] = timers[i];
        //args.timers[i] = timers[i];
    }
/*
    struct itimerspec timeout;

    timeout.it_value.tv_sec = 1;
    timeout.it_value.tv_nsec = 0;
    timeout.it_interval.tv_sec = 0;
    timeout.it_interval.tv_nsec = 0;
    timer_settime(timers[1], 0, &timeout, NULL);

    while(1){
        ;
    }

*/
    res = pthread_create(&tid[0], NULL, ack_thread, (void*)&args[0]);//---------------------------
    if(res == -1){
        err_handler(who, "pthread_create");
    }

    wnd.wnd_buff[pack.seq_num % N] = pack;//---------------------------------------
    send_packet(sockfd, pack, servaddr);//METTERE SOPRA PRIMA DI THREAD
    ///////////////////////////////////////////////////////////////////////////////SET TIMER
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

    //-------------------------------//////////////////////////////////////00000000000000000000000000000000000000000


    send_args.sockfd = sockfd;
    send_args.wnd = &wnd;
    send_args.servaddr = servaddr;

    //args.servaddr = servaddr;

    for(int i;i<READY_SIZE;i++){
        send_args.slots[i] = 0;
    }
    res = pthread_create(&tid[1], NULL, send_thread, (void*)&send_args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    int slot = 0;
    while((res = read(fd, (void*)&(pack.data[actread]), 1)) != 0){//BUFFER CON PACCHETTI PRONTI DA SPEDIRE todo
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



            sleep(13);
            printf("\nUSCITO DA PAUSE\n");//todo
        }
    }

/*
    while((res = read(fd, (void*)&(pack.data[actread]), 1)) != 0){//BUFFER CON PACCHETTI PRONTI DA SPEDIRE todo
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

            //wnd.sup = pack.seq_num;//--------------------------------------------------------
            if((pack.seq_num - wnd.inf) > 0){
                while((pack.seq_num - wnd.inf) > N);
            }
            else if((pack.seq_num - wnd.inf) < 0){
                while((MAX_SEQ_NUM - (pack.seq_num - wnd.inf)) > N);
            }////////////////////////------------------------------------------------------


            wnd.wnd_buff[pack.seq_num % N] = pack;//timer------------------------------------------------
            wnd.acked[pack.seq_num % N] = 0;
            wnd.sup = pack.seq_num;//////////PRIMA DI SPEDIRE
            send_packet(sockfd, pack, servaddr);///SCARTA CON PROBABILITA p-------------------
            printf("\ninviato pack: %d\n", pack.seq_num);//todo

            memset((void*)pack.data, 0, DATA_SIZE);
        }
    }*/
    pack.last = 1;
    pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;

    while(send_args.slots[slot] == 1);
    send_args.ready[slot] = pack;
    send_args.slots[slot] = 1;
    /* ////CONTROLLO FINESTRA-------------------------------------------------------------
     if((pack.seq_num - wnd.inf) > 0){
         while((pack.seq_num - wnd.inf) > N);
     }
     else if((pack.seq_num - wnd.inf) < 0){
         while((MAX_SEQ_NUM - (pack.seq_num - wnd.inf)) > N);
     }////////////////////////------------------------------------------------------

     wnd.wnd_buff[pack.seq_num % N] = pack;//timer------------------------------------------------
     wnd.acked[pack.seq_num % N] = 0;
     wnd.sup = pack.seq_num;//////////PRIMA DI SPEDIRE
     //printf("\ncontenuto: \n %s \n", pack.data);
     send_packet(sockfd, pack, servaddr);
     printf("\ninviato pack: %d\n", pack.seq_num);//todo*/

    //for(int n=0;n<N;n++){
    for(int i=0;i<2;i++){
        pthread_join(tid[i], NULL);
    }
    //}
    /* while(semop(sem_stdout, &w_stdo, 1) == -1){
         if(errno != EINTR){
             err_handler(who, "semop");
         }
     }*/
    printf("\nClient FILE %s sent\n", filename);///ASPETTA CONFERMA DA SERVER(manda ACK)
    kill(ppid, SIGUSR1);
    /*res = semop(sem_stdout, &s_stdo, 1);
    if(res == -1){
        err_handler(who, "semop");
    }*/
    exit(0);
}
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
        //semaforo get
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
            if (res == -1) {
                err_handler(who, "semop");
            }
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd == -1) {
                err_handler(who, "socket");
            }
            pid = fork();
            if (pid == -1) {
                err_handler(who, "fork");
            } else if (pid == 0){
                close_server(sockfd, servaddr);
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