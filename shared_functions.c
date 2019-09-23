#include "imports.h"
#include "data_format.h"
#define P 30
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
