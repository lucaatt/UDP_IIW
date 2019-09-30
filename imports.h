#ifndef UDP_IIW_VARIABLES_H
#define UDP_IIW_VARIABLES_H

#define SERV_PORT 1234
#define ADAPTIVE 1
//1 se TIMEOUT ADATTATIVO 0 altrimenti
#define BETA 0.25f
#define ALPHA 0.125f
#define MAX_NANOSEC 1000000000
//costante usata nel calclo di Estimated RTT [= (1-a)*EstRTT + a*SampleRTT]
#define CMD_SIZE 128

#define MAX_FILENAME_SIZE 128
#define NUM_FILENAME_IN_PACK 11

#define DEF_TO_SEC 0
//default timeout seconds
#define DEF_TO_NSEC 3000000
#define MAX_WAITING_TIME_SEC 4

#define CALC_TIMEOUT_THREAD 1

#define TEST 0

#define AUDIT_SEND 1
#define AUDIT_CONNECTION 0
#define AUDIT_THR 0
#define AUDIT_WND 0
#define AUDIT_ACK 0
#define AUDIT_TO 0
#define AUDIT 0

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <dirent.h>

#endif
