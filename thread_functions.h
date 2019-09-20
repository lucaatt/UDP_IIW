#ifndef UDP_IIW_THREAD_FUNCTIONS_H
#define UDP_IIW_THREAD_FUNCTIONS_H

void * send_thread(void * arg);
void * ack_thread(void * arg);
void retransmission_thread(union sigval arg);


#endif
