#include "imports.h"
#include "data_format.h"

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

void send_packet(int sockfd, struct packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
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

void send_ctrl_packet(int sockfd, struct ctrl_packet pack, struct sockaddr_in servaddr) {
    int sent = 0;
    int res;
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