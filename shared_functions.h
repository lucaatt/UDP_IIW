#ifndef UDP_IIW_SHARED_FUNCTIONS_H
#define UDP_IIW_SHARED_FUNCTIONS_H

void err_handler(char *who, char *what);
void getfrom_stdin(char *dest, char *mess, char *who, char *what);
void send_packet(int sockfd, struct packet pack, struct sockaddr_in servaddr);
void send_ctrl_packet(int sockfd, struct ctrl_packet pack, struct sockaddr_in servaddr);
void retransmission_thread(union sigval arg);

#endif
