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

    send_ctrl_packet(sockfd, snd_pack, *addr);

    len = sizeof(*addr);
    if (ctrl_pack->cmd == 1 || ctrl_pack->cmd == 2) {
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

    wnd.inf = pack.seq_num;
    wnd.sup = pack.seq_num;

    len = sizeof(addr);
    printf(who, "%s", "Server put_request_handler");

    ack_pack.ack = 1;
    ack_pack.ack_num = pack.seq_num;
    printf("\ninviato ACK: %d\n", ack_pack.ack_num);//todo
    send_ctrl_packet(sockfd, ack_pack, addr);

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
    while (1) {
        memset((void *) pack.data, 0, sizeof(DATA_SIZE));
        res = recvfrom(sockfd, (void *) &pack, sizeof(pack), 0,
                       (struct sockaddr *) &addr, &len);
        printf("\nricevuto pack: %d\n", pack.seq_num);//todo
        if (res < 0) {
            err_handler(who, "recvfrom");
        }
        if (((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf) {
            if ((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)) {
                if (pack.seq_num > wnd.sup) {
                    wnd.sup = pack.seq_num;
                }
                wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                ack_pack.ack_num = pack.seq_num;
                sleep(1);//todo
                printf("\ninviato ACK: %d\n", pack.seq_num);//todo
                send_ctrl_packet(sockfd, ack_pack, addr);
                if (pack.seq_num == wnd.inf + 1) {
                    while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                        //scrivi su file(ACKED A 0) da wnd_buff THREAD
                        res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf + 1) % N].data);
                        printf("\nscritto: %d\n", wnd.inf + 1);//todo
                        if (res < 0) {
                            err_handler(who, "fprintf");
                        }
                        fflush(file);
                        if (wnd.wnd_buff[(wnd.inf + 1) % N].last == 1) {/////ESCI SE TUTTO IN BUFF SALVATO
                            printf("\nServer saved %s successfully\n", path);
                            ack_pack.ack = 0;
                            ack_pack.fin = 1;
                            send_ctrl_packet(sockfd, ack_pack, addr);
                            exit(0);
                        }
                        wnd.acked[(wnd.inf + 1) % N] = 0;//--------------------------------------------
                        wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;

                    }
                }
            } else if (pack.seq_num <= wnd.inf) {
                ack_pack.ack_num = pack.seq_num;
                send_ctrl_packet(sockfd, ack_pack, addr);
            }
        } else if (((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf) {
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
        }
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
    int actread;
    int fd, res;
    char path[1520], *filename, who[11];
    struct ctrl_packet ctrl_pack;
    struct window wnd;
    int n;
    unsigned int temp;
    pthread_t tid[2];


    struct send_thread_args send_args;
    struct ack_thread_args args[N];
    struct sigevent sig_to;
    pthread_spinlock_t locks[N];

    temp = pack.seq_num;
    pack.seq_num = pack.ack_num;
    pack.ack_num = temp;

    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&wnd, 0, sizeof(wnd));
    sprintf(who, "%s", "Server GET");

    sprintf(path, "./server_files/%s", pack.data);

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
    /*
     * INVIO PACK.ACK = 0 SE FILE NON ESISTE
     */
    while((fd = open(path, O_RDONLY)) == -1){
        if(errno != EINTR){
            pack.ack = 0;
            pack.seq_num = pack.seq_num + 1;
            send_packet(sockfd, pack, addr);//todo gestire perdita
            //mettere timer e ack_thread
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
    //riga 197 inizializzato
    wnd.inf = pack.seq_num;// INF -> ULTIMO #SEQ CON ACK RICEVUTO IN ORDINE
    //riga 282 -> SUP relativo al primo pacchetto inserito nel buffer
                    //di pacchetti pronti per essere spediti(da send_thread)
    wnd.sup = pack.seq_num + 1;//     SUP -> ULTIMO #SEQ SERVER SPEDITO
    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }

    /*
     * TIMEOUT E RITRASMISSIONE
     */
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
        args[i].servaddr = addr;
        timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
    }



    send_args.sockfd = sockfd;
    send_args.wnd = &wnd;
    send_args.servaddr = addr;
    /*
     * ARRAY PER INDICARE A SEND_THREAD I PACCHETI PRONTI DA SPEDIRE, PREPARATI DAL MAIN E INSERITI IN
     *                                                                        SEND_ARGS.READY[]
     */
    for(int i;i<READY_SIZE;i++){
        send_args.slots[i] = 0;
    }
    res = pthread_create(&tid[1], NULL, send_thread, (void*)&send_args);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    res = pthread_create(&tid[0], NULL, ack_thread, (void*)&args[0]);
    if(res == -1){
        err_handler(who, "pthread_create");
    }
    int slot = 0;
    actread = 0;
    pack.ack = 1;
    memset((void*)pack.data, 0, sizeof(DATA_SIZE));

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

    printf("\nServer FILE %s sent\n", path);
    exit(0);

}

/*
void request_handler(int sockfd, struct packet pack, int cmd, struct sockaddr_in addr) {
    int fd, len, res;
    int actread;
    FILE *file;
    char path[1520], *filename;
    char buff[1024];
    char who[40];
    struct ctrl_packet ack_pack;
    struct ctrl_packet ctrl_pack;
    pthread_t tid;
    struct window wnd;
    int var;
    pid_t pid;
    int n;

    struct send_thread_args send_args;
    struct ack_thread_args args[N];
    struct sigevent sig_to;

    for (n = 0; n < N; n++) {
        wnd.acked[n] = 0;
    }

    wnd.inf = pack.seq_num;//DENTRO IF CMD = 1
    wnd.sup = pack.seq_num;

    len = sizeof(addr);
    sprintf(who, "%s", "Server request_handler");



    //////THREAD PER ACK #SEQ CLIENT IN PACK [SELECTIVE REPEAT DA QUA IN POI]
    if (cmd == 2) {
        pid = getpid();
        memset((void *) &ctrl_pack, 0, sizeof(ctrl_pack));
        memset((void *) &wnd, 0, sizeof(wnd));
        memset((void *) &pack, 0, sizeof(pack));
        filename = (char *) malloc(MAX_FILENAME_SIZE);
        if (filename == NULL) {
            close(sockfd);
            err_handler(who, "malloc");
        }

        res = sprintf(filename, "%s", pack.data);
        if (res < 0) {
            close(sockfd);
            err_handler(who, "sprintf");
        }
        pack.seq_num = pack.ack_num + 1;
        pack.ack = 0;
        pack.seq_num = ctrl_pack.seq_num + 1;
        pack.ack_num = ctrl_pack.ack_num;
        //pack.ack = 1;

        wnd.inf = pack.seq_num - 1;
        wnd.sup = pack.seq_num;
        for (n = 0; n < N; n++) {
            wnd.acked[n] = 0;
        }

        //pthread_t tid;
        pthread_t tid[2];

        sig_to.sigev_notify = SIGEV_THREAD;
        sig_to.sigev_notify_function = &retransmission_thread;
        //sig_to.sigev_notify_function = &ack_thread;
        //sig_to.sigev_value.sival_ptr = (void*)&args;
        timer_t timers[N];
        send_args.timers = timers;
        //args.timers = timers;
        for (int i = 0; i < N; i++) {
            //sig_to.sigev_value.sival_int = i;
            sig_to.sigev_value.sival_ptr = (void *) &args[i];
            args[i].sockfd = sockfd;
            args[i].wnd = &wnd;
            args[i].timer_num = i;
            args[i].timers = timers;
            args[i].servaddr = addr;
            timer_create(CLOCK_MONOTONIC, &sig_to, &timers[i]);
            //send_args.timers[i] = timers[i];
            //args.timers[i] = timers[i];
        }
        res = pthread_create(&tid[0], NULL, ack_thread, (void *) &args[0]);//---------------------------
        if (res == -1) {
            err_handler(who, "pthread_create");
        }

        wnd.wnd_buff[pack.seq_num % N] = pack;//---------------------------------------
        send_packet(sockfd, pack, addr);//METTERE SOPRA PRIMA DI THREAD
        ///////////////////////////////////////////////////////////////////////////////SET TIMER
        while ((wnd.sup - wnd.inf) != 0);

        sprintf(buff, "./server_files/%s", filename);
        while ((fd = open(buff, O_RDONLY)) == -1) {
            if (errno != EINTR) {
                err_handler(who, "open");
            }
        }
        actread = 0;
        pack.ack = 0;
        memset((void *) pack.data, 0, sizeof(DATA_SIZE));

        //-------------------------------//////////////////////////////////////00000000000000000000000000000000000000000


        send_args.sockfd = sockfd;
        send_args.wnd = &wnd;
        send_args.servaddr = addr;

        //args.servaddr = servaddr;

        for (int i; i < READY_SIZE; i++) {
            send_args.slots[i] = 0;
        }
        res = pthread_create(&tid[1], NULL, send_thread, (void *) &send_args);
        if (res == -1) {
            err_handler(who, "pthread_create");
        }
        int slot = 0;
        while ((res = read(fd, (void *) &(pack.data[actread]), 1)) != 0) {//BUFFER CON PACCHETTI PRONTI DA SPEDIRE todo
            if (res == -1) {
                if (errno != EINTR) {
                    err_handler(who, "read");
                }
            }
            actread++;
            actread = actread % DATA_SIZE;
            if (actread == DATA_SIZE - 1) {
                pack.data[actread] = '\0';
                actread++;
                actread = actread % DATA_SIZE;
                pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;

                while (send_args.slots[slot] == 1);
                send_args.ready[slot] = pack;
                send_args.slots[slot] = 1;
                slot = (slot + 1) % READY_SIZE;

                memset((void *) pack.data, 0, DATA_SIZE);


                sleep(13);
                printf("\nUSCITO DA PAUSE\n");//todo
            }
        }


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
    }
        pack.last = 1;
        pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;

        while (send_args.slots[slot] == 1);
        send_args.ready[slot] = pack;
        send_args.slots[slot] = 1;
         ////CONTROLLO FINESTRA-------------------------------------------------------------
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
         printf("\ninviato pack: %d\n", pack.seq_num);//todo

        //for(int n=0;n<N;n++){
        for (int i = 0; i < 2; i++) {
            pthread_join(tid[i], NULL);
        }
        //}
         while(semop(sem_stdout, &w_stdo, 1) == -1){
             if(errno != EINTR){
                 err_handler(who, "semop");
             }
         }
        printf("\nServer FILE %s sent\n", filename);///ASPETTA CONFERMA DA SERVER(manda ACK)
        res = semop(sem_stdout, &s_stdo, 1);
        if(res == -1){
            err_handler(who, "semop");
        }


        struct ack_thread_args args;
        args.sockfd = sockfd;
        args.wnd = &wnd;
        res = pthread_create(&tid, NULL, ack_thread, (void *) &args);
        if (res == -1) {
            close(sockfd);
            err_handler(who, "pthread_create");
        }

        wnd.wnd_buff[pack.seq_num % N] = pack;
        send_packet(sockfd, pack, addr);
        while ((wnd.sup - wnd.inf) != 0);

        sprintf(path, "./server_files/%s", filename);
        while ((fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0660)) == -1) {
            if (errno != EINTR) {
                err_handler(who, "open");
            }
        }
        actread = 0;
        pack.ack = 0;
        memset((void *) pack.data, 0, sizeof(DATA_SIZE));
        while ((res = read(fd, (void *) &(pack.data[actread]), 1)) != 0) {
            if (res == -1) {
                if (errno != EINTR) {
                    close(sockfd);
                    err_handler(who, "read");
                }
            }
            actread++;
            actread = actread % DATA_SIZE;
            if (actread == DATA_SIZE - 1) {
                pack.data[actread] = '\0';
                actread++;
                actread = actread % DATA_SIZE;
                pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;
                if ((pack.seq_num - wnd.inf) > 0) {
                    while ((pack.seq_num - wnd.inf) > N);
                } else if ((pack.seq_num - wnd.inf) < 0) {
                    while ((MAX_SEQ_NUM - (pack.seq_num - wnd.inf)) > N);
                }


                wnd.wnd_buff[pack.seq_num % N] = pack;
                wnd.acked[pack.seq_num % N] = 0;
                wnd.sup = pack.seq_num;
                send_packet(sockfd, pack, addr);
                printf("\ninviato pack: %d\n", pack.seq_num);

                memset((void *) pack.data, 0, DATA_SIZE);
            }
        }
        pack.last = 1;
        pack.seq_num = (pack.seq_num + 1) % MAX_SEQ_NUM;
        if ((pack.seq_num - wnd.inf) > 0) {
            while ((pack.seq_num - wnd.inf) > N);
        } else if ((pack.seq_num - wnd.inf) < 0) {
            while ((MAX_SEQ_NUM - (pack.seq_num - wnd.inf)) > N);
        }

        wnd.wnd_buff[pack.seq_num % N] = pack;
        wnd.acked[pack.seq_num % N] = 0;
        wnd.sup = pack.seq_num;
        send_packet(sockfd, pack, addr);
        printf("\ninviato pack: %d\n", pack.seq_num);


        while (wnd.acked[wnd.sup % N] == 0);

        printf("\nServer FILE %s inviato\n", filename);
        kill(pid, SIGUSR1);

        printf("\nfine cmd 2\n");
        exit(0);

    } else if (cmd == 1) {
        ack_pack.ack = 1;//-------------------------------------------------------------
        ack_pack.ack_num = pack.seq_num;
        printf("\ninviato ACK: %d\n", ack_pack.ack_num);//todo
        send_ctrl_packet(sockfd, ack_pack, addr);

        printf("\nServer saving file %s from Client...\n", pack.data);
        sprintf(path, "./server_files/%s", pack.data);
        //res = open(path, O_CREAT|O_RDWR|O_EXCL, 0660); //chiedere se sovrascrivere
        while ((fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0660)) == -1) {
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
            res = recvfrom(sockfd, (void *) &pack, sizeof(pack), 0,
                           (struct sockaddr *) &addr, &len);
            printf("\nricevuto pack: %d\n", pack.seq_num);//todo
            if (res < 0) {
                err_handler(who, "recvfrom");
            }
            //----------------------------------------------------------------------------
            if (((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf) {
                if ((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)) {
                    if (pack.seq_num > wnd.sup) {
                        wnd.sup = pack.seq_num;
                    }
                    wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                    wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                    ack_pack.ack_num = pack.seq_num;
                    sleep(5);//todo
                    printf("\ninviato ACK: %d\n", pack.seq_num);//todo
                    send_ctrl_packet(sockfd, ack_pack, addr);
                    if (pack.seq_num == wnd.inf + 1) {
                        while ((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)) {
                            //scrivi su file(ACKED A 0) da wnd_buff THREAD
                            res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf + 1) % N].data);
                            printf("\nscritto: %d\n", wnd.inf + 1);//todo
                            if (res < 0) {
                                err_handler(who, "fprintf");
                            }
                            fflush(file);
                            if (wnd.wnd_buff[(wnd.inf + 1) % N].last == 1) {/////ESCI SE TUTTO IN BUFF SALVATO
                                printf("\nServer saved %s successfully\n", path);
                                ack_pack.ack = 0;
                                ack_pack.fin = 1;
                                send_ctrl_packet(sockfd, ack_pack, addr);
                                exit(0);
                            }
                            wnd.acked[(wnd.inf + 1) % N] = 0;//--------------------------------------------
                            wnd.inf = (wnd.inf + 1) % MAX_SEQ_NUM;

                        }
                    }
                } else if (pack.seq_num <= wnd.inf) {
                    ack_pack.ack_num = pack.seq_num;
                    send_ctrl_packet(sockfd, ack_pack, addr);
                }
            } else if (((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf) {
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
            }
        }

    } else if (cmd == 3) {

    }
}
*/

//TODO AGGIUNGERE SIGCHILD IGNORE
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
            else{
                printf("\nrequest from client not recognized\n");
                exit(0);
            }
            //request_handler(connection_sockfd, pack, cmd, addr);

        }


    }
}

