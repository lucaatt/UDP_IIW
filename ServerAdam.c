#include "imports.h"
#include "data_format.h"
#include "shared_functions.h"

int handshake_server(int sockfd, struct ctrl_packet *ctrl_pack, struct packet *pack, struct sockaddr_in *addr) {
    struct ctrl_packet snd_pack;
    snd_pack.seq_num = 0;//rand();
    snd_pack.syn = 1;
    snd_pack.ack_num = ctrl_pack->seq_num;
    snd_pack.ack = 1;
    int len, res;

    /*ifdef AUDIT
    printf("\nserver #seq: %d\nserver #ack: %d\n", snd_pack.seq_num, snd_pack.ack_num);
    */

    send_ctrl_packet(sockfd, snd_pack, *addr);

    /*
    struct sockaddr_in servaddr;
    memset((void*)&servaddr, 0, sizeof(servaddr));
    socklen_t len1 = sizeof(servaddr);
    if(getsockname(sockfd, (struct sockaddr *)&servaddr, &len1) == -1){
        printf("\nerr getsock\n");
        exit(-1);
    }
    printf("\nport %d\n", ntohs(servaddr.sin_port));
    */

    len = sizeof(*addr);
    //////COMANDO PUT -> ULTIMO ACK HANDSHAKE IN PIGGYBACK CON DATI(NOME FILE)
    if(ctrl_pack->cmd == 1){
        res = recvfrom(sockfd, (void *)pack, sizeof(*pack), 0,
                       (struct sockaddr *)addr, &len);//////////if res < size pack non mandare ack
        if (res < 0) {
            return -1;
        }

        //printf("\nack da client: %d\n#seq client: %d\n", pack->ack_num, pack->seq_num);
        //verifying ack
        if (pack->ack == 1) {
            if (pack->ack_num != snd_pack.seq_num) {
                return -1;
            }
            if(pack->seq_num != (ctrl_pack->seq_num) + 1){
                return -1;
            }
            return 0;
        } else {
            return -1;
        }
    }
    /////GESTIRE ALTRI COMANDI [TERZO ACK NON IN PIGGYBACK??]
    return 0;
}

void request_handler(int sockfd, struct packet pack, int cmd, struct sockaddr_in addr){
    int fd, len, res;
    FILE *file;
    char path[1520];
    char who[40];
    struct ctrl_packet ack_pack;
    struct window wnd;
    int var;
    int n;

    for(n=0;n<N;n++){
        wnd.acked[n] = 0;
    }

    wnd.inf = pack.seq_num;
    wnd.sup = pack.seq_num;

    len = sizeof(addr);
    printf(who, "%s", "Server request_handler");



    //////THREAD PER ACK #SEQ CLIENT IN PACK [SELECTIVE REPEAT DA QUA IN POI]

    if(cmd == 1){
        ack_pack.ack = 1;//-------------------------------------------------------------
        ack_pack.ack_num = pack.seq_num;
        printf("\ninviato ACK: %d\n", ack_pack.ack_num);//todo
        send_ctrl_packet(sockfd, ack_pack, addr);

        printf("\nServer saving file %s from Client...\n", pack.data);
        sprintf(path, "./server_files/%s", pack.data);
        //res = open(path, O_CREAT|O_RDWR|O_EXCL, 0660); //chiedere se sovrascrivere
        while((fd = open(path, O_CREAT|O_RDWR|O_TRUNC, 0660)) == -1){
            if(errno != EINTR){
                err_handler(who, "open");
            }
        }
        file = fdopen(fd, "w+");
        if(file == NULL){
            err_handler(who, "fdopen");
        }
        while(1){
            memset((void*)pack.data, 0, sizeof(DATA_SIZE));
            res = recvfrom(sockfd, (void*)&pack, sizeof(pack), 0,
                           (struct sockaddr *)&addr, &len);
            printf("\nricevuto pack: %d\n", pack.seq_num);//todo
            if(res < 0){
                err_handler(who, "recvfrom");
            }
            //----------------------------------------------------------------------------
            if(((wnd.inf + N) % MAX_SEQ_NUM) > wnd.inf){
                if((pack.seq_num <= wnd.inf + N) && (pack.seq_num > wnd.inf)){
                    if(pack.seq_num > wnd.sup){
                        wnd.sup = pack.seq_num;
                    }
                    wnd.wnd_buff[pack.seq_num % N] = pack;//send ACK
                    wnd.acked[pack.seq_num % N] = 1;// INIZIALIZZA TUTTI A 0
                    ack_pack.ack_num = pack.seq_num;
                    //sleep(1);//todo
                    printf("\ninviato ACK: %d\n", pack.seq_num);//todo
                    send_ctrl_packet(sockfd, ack_pack, addr);
                    if(pack.seq_num == wnd.inf + 1){
                        while((wnd.acked[(wnd.inf + 1) % N] == 1) && (wnd.inf < wnd.sup)){
                            //scrivi su file(ACKED A 0) da wnd_buff THREAD
                            res = fprintf(file, "%s", wnd.wnd_buff[(wnd.inf+1)%N].data);
                            printf("\nscritto: %d\n", wnd.inf+1);//todo
                            if(res < 0){
                                err_handler(who, "fprintf");
                            }
                            fflush(file);
                            if(wnd.wnd_buff[(wnd.inf+1)%N].last == 1){/////ESCI SE TUTTO IN BUFF SALVATO
                                printf("\nServer saved %s successfully\n", path);
                                ack_pack.ack = 0;
                                ack_pack.fin = 1;
                                send_ctrl_packet(sockfd, ack_pack, addr);
                                exit(0);
                            }
                            wnd.acked[(wnd.inf + 1) % N] = 0;//--------------------------------------------
                            wnd.inf = (wnd.inf + 1)%MAX_SEQ_NUM;

                        }
                    }
                }
                else if(pack.seq_num <= wnd.inf){
                    ack_pack.ack_num = pack.seq_num;
                    send_ctrl_packet(sockfd, ack_pack, addr);
                }
            }
            else if(((wnd.inf + N) % MAX_SEQ_NUM) < wnd.inf){
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
                    send_ctrl_packet(sockfd, ack_pack, addr);
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
                    send_ctrl_packet(sockfd, ack_pack, addr);
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
                    ack_pack.ack_num = pack.seq_num;
                    send_ctrl_packet(sockfd, ack_pack, addr);
                }
            }

           /*
            ////////SALVA PACK DATA IN BUFFER SE FUORI ORDINE
            //printf("\ncontenuto: %s\n", pack.data);
            res = fprintf(file, "%s", pack.data);
            if(res < 0){
                err_handler(who, "fprintf");
            }
            fflush(file);
            if(pack.last == 1){/////ESCI SE TUTTO IN BUFF SALVATO
                printf("\nServer saved %s successfully\n", path);
                exit(0);
            }*/
        }

    }
}

int main(int argc, char *argv[]){
    int listen_sockfd, connection_sockfd;
    int res, len, cmd;
    struct sockaddr_in addr;
    struct packet pack;
    struct ctrl_packet ctrl_pack;
    char who[7];
    pid_t pid;

    sprintf(who, "%s", "Server");
    memset((void*)&ctrl_pack, 0, sizeof(ctrl_pack));
    memset((void*)&pack, 0, sizeof(pack));
    memset((void*)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(SERV_PORT);

    printf("Server running...\n\n");

    if((listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        err_handler(who, "socket");
    }
    if(bind(listen_sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        err_handler(who, "bind");
    }
    len = sizeof(addr);

    while(1){
        printf("\nWaiting for a request\n");
        res = recvfrom(listen_sockfd, (void*)&ctrl_pack, sizeof(ctrl_pack), 0,
                (struct sockaddr *)&addr, &len);//////////if res < size pack non mandare ack
        if(res < 0){
            err_handler(who, "recvfrom");
        }

        cmd = ctrl_pack.cmd;
        //////COMANDO STA IN CTRL_PACK
        /////////////printf ctrl_pack #sequenza e #PORT in addr-> del client
        ////////////////////fork e nuova socket con diversa porta
        if((connection_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
            err_handler(who, "socket");
        }

        res = handshake_server(connection_sockfd, &ctrl_pack, &pack, &addr);
        //PACK ha #seq client e SERVER e variabile con filename dal client(PACK)
        if(res == -1){
            err_handler(who, "handshake_server");
        }

        pid = fork();
        if(pid == -1){
            err_handler(who, "fork");
        }
        if(pid == 0){
            request_handler(connection_sockfd, pack, cmd, addr);
        }
    }

}
