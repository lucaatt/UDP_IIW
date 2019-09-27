#include "imports.h"
#include "data_format.h"
#define P 20
//probabilità di perdita pacchetto(scartato da send_packet) in percentuale


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


/*
 * usata per inviare un pacchetto (pack -> vedere data_format) dalla SOCKET sockfd all'indirizzo in servaddr
 */
void send_packet(int sockfd, struct packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
    float p = ((float)P)/100.0;
    float outcome;//se minore di p scarta il pacchetto altrimenti lo invia
    outcome = ((float)rand())/((float)RAND_MAX);
    if(outcome > p) {
        if(AUDIT_SEND == 1){
            printf("\npacchetto %d inviato\n",pack.seq_num);
        }
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
    else{
        if(AUDIT_SEND == 1) {
            printf("\n-----pacchetto %d scartato------\n", pack.seq_num);
        }
    }
}

void send_ctrl_packet(int sockfd, struct ctrl_packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
    float p = ((float)P)/100.0;
    float outcome;//se minore di p scarta il pacchetto altrimenti lo invia
    outcome = ((float)rand())/((float)RAND_MAX);
    //printf("\np %f, P %d, outcome %f", p, P, outcome);
    if(outcome > p) {
        if(AUDIT_SEND == 1){
            printf("\nack %d inviato\n",pack.ack_num);
        }
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
    else{
        if(AUDIT_SEND == 1) {
            printf("\n-----ack %d scartato-----\n", pack.ack_num);
        }
    }
}

int start_close_connection(int sockfd, int wnd_inf, struct sockaddr_in addr){
    struct ctrl_packet close_pack;
    struct ctrl_packet ack_pack;
    struct packet rcv_pack;
    int len, res;
    struct timespec close_connection_timeout = {2,0};
    int attempts = 0;

    close_pack.ack = 0;
    close_pack.fin = 1;
    close_pack.seq_num = 123;

    ack_pack.ack = 1;
    ack_pack.fin = 0;

    if(AUDIT == 1){
        printf("\nstart close connection\n");
    }
    send_ctrl_packet(sockfd, close_pack, addr);

    len = sizeof(addr);

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&close_connection_timeout, sizeof(close_connection_timeout))<0){
        err_handler("Close Connection", "setsockopt");
    }

    while(1){
        if(recvfrom(sockfd, (void *) &rcv_pack, sizeof(rcv_pack), 0, (struct sockaddr *)&addr, &len) < 0) {
            if (errno != EINTR) {
                if (errno == EWOULDBLOCK) {
                    attempts++;
                    if (attempts == 4) {
                        printf("\n%d attempts to close connection. EXITING\n\n", attempts);
                        return (-1);
                    }
                    printf("\nClosing connection attempt number %d. Trying again...\n", attempts);
                    close_connection_timeout.tv_sec = (close_connection_timeout.tv_sec) * 2;
                    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &close_connection_timeout,
                                   sizeof(close_connection_timeout)) < 0) {
                        err_handler("Close connection", "setsockopt");
                    }
                    send_ctrl_packet(sockfd, close_pack, addr);
                } else {
                    err_handler("Close connection", "recvfrom");
                }
            }
        }
        else if(rcv_pack.fin == 0 && rcv_pack.seq_num <= wnd_inf) {
            if(AUDIT == 1){
                printf("\nCC ricevuto #SEQ <= INF, %d", rcv_pack.seq_num);
            }
            ack_pack.ack_num = rcv_pack.seq_num;
            send_ctrl_packet(sockfd, ack_pack, addr);

        }
        else if(rcv_pack.ack == 1 && rcv_pack.fin == 1){
            attempts = 0;
            close_connection_timeout.tv_sec = 2;
            close_connection_timeout.tv_nsec = 0;
            if(AUDIT == 1){
                printf("\nricevuto ACK FIN\n");
            }
            if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&close_connection_timeout, sizeof(close_connection_timeout))<0){
                err_handler("Close Connection", "setsockopt");
            }
            while(1) {
                while (recvfrom(sockfd, (void *) &close_pack, sizeof(close_pack), 0, (struct sockaddr *) &addr, &len) <
                       0) {
                    if (errno != EINTR) {
                        if (errno == EWOULDBLOCK) {
                            attempts++;
                            if (attempts == 3) {
                                printf("\nWaited %d times for message to close connection.EXITING...\n\n", attempts);
                                return (-1);
                            }
                            printf("\nWaiting for FIN message (attempt %d)...\n", attempts);
                            close_connection_timeout.tv_sec = (close_connection_timeout.tv_sec) * 2;
                            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &close_connection_timeout,
                                           sizeof(close_connection_timeout)) < 0) {
                                err_handler("Close connection", "setsockopt");
                            }
                        } else {
                            err_handler("Close connection", "recvfrom");
                        }
                    }
                }
                if (close_pack.fin == 1 && close_pack.ack == 0) {
                    if(AUDIT == 1){
                        printf("\nricevuto ultimo FIN\n");
                    }
                    close_pack.ack = 1;
                    send_ctrl_packet(sockfd, close_pack, addr);
                    printf("\nCONNECTION CLOSED. EXITING...\n");
                    return (0);
                }
            }
        }
    }
}

int confirm_close_connection(int sockfd, struct sockaddr_in addr){
    struct packet close_pack;
    struct ctrl_packet close_ctrlpack;
    struct ctrl_packet ack_close;
    struct timespec close_connection_timeout = {2,0};
    int attempts = 0;
    int len = sizeof(addr);

    close_pack.ack = 1;
    close_pack.fin = 1;
    close_pack.seq_num = 123;

    close_ctrlpack.fin = 1;
    close_ctrlpack.ack = 0;
    close_ctrlpack.ack_num = 123;
    if(AUDIT == 1){
        printf("\nstart confirm close\n");
    }

    send_packet(sockfd, close_pack, addr);

    send_ctrl_packet(sockfd, close_ctrlpack, addr);

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&close_connection_timeout, sizeof(close_connection_timeout))<0){
        err_handler("Close Connection", "setsockopt");
    }
    while(1) {
        while (recvfrom(sockfd, (void *) &ack_close, sizeof(ack_close), 0, (struct sockaddr *) &addr, &len) < 0) {
            if (errno != EINTR) {
                if (errno == EWOULDBLOCK) {
                    attempts++;
                    if (attempts == 4) {
                        printf("\n%d attempts to confirm close connection. EXITING\n\n", attempts);
                        return (-1);
                    }
                    printf("\nConfirming close connection attempt number %d. Trying again...\n", attempts);
                    close_connection_timeout.tv_sec = (close_connection_timeout.tv_sec) * 2;
                    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &close_connection_timeout,
                                   sizeof(close_connection_timeout)) < 0) {
                        err_handler("Confirm close connection", "setsockopt");
                    }
                    send_packet(sockfd, close_pack, addr);

                    send_ctrl_packet(sockfd, close_ctrlpack, addr);
                } else {
                    err_handler("Confirm close connection", "recvfrom");
                }
            }
        }
        if (ack_close.fin == 1 && ack_close.ack == 1) {
            if(AUDIT == 1){
                printf("\nultimo ACK FIN ricevuto\n");
            }
            printf("\nCONNECTION CLOSED. EXITING...\n");
            return (0);
        }
    }
}

int fnf_close_connection(int sockfd, int wnd_inf, struct sockaddr_in addr){
    struct ctrl_packet close_pack;
    struct ctrl_packet ack_pack;
    struct packet rcv_pack;
    struct packet pack;
    int len, res;
    struct timespec close_connection_timeout = {2,0};
    int attempts = 0;

    close_pack.ack = 0;
    close_pack.fin = 1;
    close_pack.seq_num = 123;

    pack.ack = 0;
    pack.fin = 1;
    pack.seq_num = 123;

    ack_pack.ack = 1;
    ack_pack.fin = 0;

    if(AUDIT == 1){
        printf("\nstart close connection\n");
    }
    send_packet(sockfd, pack, addr);

    len = sizeof(addr);

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&close_connection_timeout, sizeof(close_connection_timeout))<0){
        err_handler("Close Connection", "setsockopt");
    }

    while(1){
        if(recvfrom(sockfd, (void *) &rcv_pack, sizeof(rcv_pack), 0, (struct sockaddr *)&addr, &len) < 0) {
            if (errno != EINTR) {
                if (errno == EWOULDBLOCK) {
                    attempts++;
                    if (attempts == 4) {
                        printf("\n%d attempts to close connection. EXITING\n\n", attempts);
                        return (-1);
                    }
                    printf("\nClosing connection attempt number %d. Trying again...\n", attempts);
                    close_connection_timeout.tv_sec = (close_connection_timeout.tv_sec) * 2;
                    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &close_connection_timeout,
                                   sizeof(close_connection_timeout)) < 0) {
                        err_handler("Handshake", "setsockopt");
                    }
                    send_packet(sockfd, pack, addr);
                } else {
                    err_handler("Handshake", "recvfrom");
                }
            }
        }
        else if(rcv_pack.fin == 0 && rcv_pack.seq_num <= wnd_inf) {
            if(AUDIT == 1){
                printf("\nCC ricevuto #SEQ <= INF, %d", rcv_pack.seq_num);
            }
            ack_pack.ack_num = rcv_pack.seq_num;
            send_ctrl_packet(sockfd, ack_pack, addr);

        }
        else if(rcv_pack.ack == 1 && rcv_pack.fin == 1){
            attempts = 0;
            close_connection_timeout.tv_sec = 2;
            close_connection_timeout.tv_nsec = 0;
            if(AUDIT == 1){
                printf("\nricevuto ACK FIN\n");
            }
            if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void*)&close_connection_timeout, sizeof(close_connection_timeout))<0){
                err_handler("Close Connection", "setsockopt");
            }
            while(1) {
                while (recvfrom(sockfd, (void *) &close_pack, sizeof(close_pack), 0, (struct sockaddr *) &addr, &len) <
                       0) {
                    if (errno != EINTR) {
                        if (errno == EWOULDBLOCK) {
                            attempts++;
                            if (attempts == 3) {
                                printf("\nWaited %d times for message to close connection.EXITING...\n\n", attempts);
                                return (-1);
                            }
                            printf("\nWaiting for FIN message (attempt %d)...\n", attempts);
                            close_connection_timeout.tv_sec = (close_connection_timeout.tv_sec) * 2;
                            if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *) &close_connection_timeout,
                                           sizeof(close_connection_timeout)) < 0) {
                                err_handler("Handshake", "setsockopt");
                            }
                        } else {
                            err_handler("Close connection", "recvfrom");
                        }
                    }
                }
                if (close_pack.fin == 1 && close_pack.ack == 0) {
                    if(AUDIT == 1){
                        printf("\nricevuto ultimo FIN\n");
                    }
                    close_pack.ack = 1;
                    send_ctrl_packet(sockfd, close_pack, addr);
                    printf("\nCONNECTION CLOSED. EXITING...\n");
                    return (0);
                }
            }
        }
    }
}