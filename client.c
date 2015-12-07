#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "helperlib.h"
#include <errno.h>
#include <sys/time.h>
/*
 * usage : ./client ip port num_thread num_request filename
 */
const char DEFAULT_IP[128] = "127.0.0.1";
const int DEFAULT_PORT = 5000;
const int DEFAULT_NUM_THREAD = 10;
const char DEFAULT_FILENAME[128] = "index.html";
const char BASE_PATH[128] = "templates/";

char IP[128];
int port_number;
int num_thread;
char filename[128];
int NUM_REQUEST = 30;

#define MAX_NUM_THREAD 10000
#define MAX_LEN_RESPONSE 10000
#define MAX_LEN_REQUEST 10000

struct sockaddr_in serverAddr;
pthread_mutex_t mutex_print;
int numFailRecv = 0;
int numFailConn = 0;

char *generate_request() {
    char buf[MAX_LEN_REQUEST];
    sprintf(buf,"GET %s HTTP/1.1",filename);
    return copy_str_dynamic(buf);
}
void * send_request(void * threadId) {
    int id = *((int *) threadId);
    int clientSocket, lenBuffer;
    char *request;
    int curNumRequest, lenRequest;
    ssize_t numByte, numByteRecv;
    char buffer[MAX_LEN_RESPONSE], *relative_buffer;
    char end[100] = "END";
    int flagFailRecv;

    int requestBytes = 0;
    int responseBytes = 0;
    struct timeval start_point, end_point;
    double op_time;
    gettimeofday(&start_point, NULL);

    /* Create a socket */
    for(curNumRequest = 0; curNumRequest < NUM_REQUEST; curNumRequest++)
    {
        clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(clientSocket < 0)
            print_system_error("socket() fails");

        /* Establish connection to the server */
        if(connect(clientSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0)
            //              print_system_error("connect() fails");
        {
            //perror("connect() fails");
            //pthread_exit(NULL);
            pthread_mutex_lock(&mutex_print);
            numFailConn++;
            pthread_mutex_unlock(&mutex_print);
            continue;
        }

        /* Send request */
        request = generate_request();
        lenRequest = strlen(request);

        numByte = send(clientSocket, request, lenRequest, 0);
        requestBytes += numByte;
        if(numByte < 0)
            print_system_error("send() fails");
        else
            if(numByte != lenRequest)
                print_user_error("send()", "sent unexpected number of bytes");
        shutdown(clientSocket, SHUT_WR);

        /* Receive response */
        relative_buffer = buffer;
        lenBuffer = MAX_LEN_RESPONSE;
        flagFailRecv = 0;
        do
        {
            numByteRecv = recv(clientSocket, relative_buffer, lenBuffer - 1, 0);
            responseBytes += numByteRecv;
            if(numByteRecv < 0)
            {
                flagFailRecv = 1;
                break;
            }
            else
                if(numByteRecv > 0)
                {
                    relative_buffer = relative_buffer + numByteRecv;
                    lenBuffer -= numByteRecv;
                }
                else
                    relative_buffer[0] = '\0';
        } while(numByteRecv > 0);
        close(clientSocket);
        pthread_mutex_lock(&mutex_print);
        {
            if(flagFailRecv)
                numFailRecv++;
            else
            {
                printf("Client thread with id = %d sends request %d\n", id, curNumRequest + 1);
                printf("Response from the server:\n");
//                printf("%s", buffer);
            }
        }
        pthread_mutex_unlock(&mutex_print);
        free(request);
    }
    gettimeofday(&end_point, NULL);

    op_time = (double)(end_point.tv_sec)+(double)(end_point.tv_usec)/1000000.0 - (double)(start_point.tv_sec)-(double)(start_point.tv_usec)/1000000.0;
    printf("[thread number : %d] [total byte %d]\n",id,responseBytes);
    printf("[thread number : %d] [exec time %lf]\n",id,op_time);
    pthread_exit(NULL);
}

int main(int argc,char*argv[]) {
    if ( argc != 6 ) {
        printf("usage : ./client ip port num_thread num_request filename\n");
        return 0;
    }
    strcpy(IP,argv[1]);
    sscanf(argv[2],"%d",&port_number);
    sscanf(argv[3],"%d",&num_thread);
    sscanf(argv[4],"%d",&NUM_REQUEST);
    strcpy(filename,argv[5]);
    

    int numThread = num_thread, index, clientSocket, rtnValue;
    char message[128];
    pthread_t threadArr[MAX_NUM_THREAD];
    in_port_t serverPort = port_number;
    void *status;
    int pthread_id[MAX_NUM_THREAD];

    memset(&serverAddr,0,sizeof(struct sockaddr_in));
    serverAddr.sin_family = AF_INET;
    rtnValue = inet_pton(AF_INET, IP,&serverAddr.sin_addr.s_addr);
    if ( rtnValue == 0 ) {
        print_user_error("inet_pton() fails","Invalid address string");
    } else if ( rtnValue < 0 ) {
        print_system_error("inet_ptn() fails");
    }
    serverAddr.sin_port = htons(serverPort);

    pthread_mutex_init(&mutex_print, NULL);
    for ( index = 0 ; index < numThread ; index++ ) {
        pthread_id[index] = index;
        rtnValue = pthread_create(&threadArr[index], NULL, send_request,&pthread_id[index]);
        if ( rtnValue ) {
            sprintf(message, "pthread_create() fails with error code %d",rtnValue);
            print_system_error(message);
        }
    }
    for ( index = 0 ; index < numThread ; index++ ) {
        rtnValue = pthread_join(threadArr[index], &status);
    }
    printf("Total failed request by recv() = %d\n", numFailRecv);
    printf("Total failed request by connect() = %d\n", numFailConn);
//    printf("request bytes : %lf response bytes : %lf\n",(double)requestBytes/NUM_REQUEST, (double)responseBytes/NUM_REQUEST);
    pthread_exit(NULL);

    return 0;
}
