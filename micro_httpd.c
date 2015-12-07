/* micro_httpd - really small HTTP server
**
** Copyright © 1999,2005 by Jef Poskanzer <jef@mail.acme.com>.
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
** OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
** HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
*/


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <pthread.h>
#include "queue.h"
#include "m_http.h"
#include "helperlib.h"

#define SERVER_NAME "micro_httpd"
#define SERVER_URL "http://www.acme.com/software/micro_httpd/"
#define PROTOCOL "HTTP/1.1"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"


/* Forwards. */
static void file_details( char* dir, char* name );
static void send_error( int status, char* title, char* extra_header, char* text );
static void send_headers( int status, char* title, char* extra_header, char* mime_type, off_t length, time_t mod );
static char* get_mime_type( char* name );
static void strdecode( char* to, char* from );
static int hexit( char c );
static void strencode( char* to, size_t tosize, const char* from );

static const int DEFAULT_NUM_WORKER = 10;
static const int MAX_WORKER = 5000;
static const int MAX_WAIT = 5;
static const int MAX_LEN_REQUEST = 800;

static const int MAX_NUM_SLASH = 2;
int port_number;
char s_port_number[128];
int num_worker;
char s_num_worker[128];

struct queue_int_s *clientQueue;
pthread_mutex_t queue_mutex;
pthread_cond_t queue_has_client;

void create_worker(pthread_t *workerArr, int *threadId_arr,int numWorker);
void *handle_client_request(void* workerId);
char *read_request(int clientSocket);

int
main( int argc, char** argv )
    {
        char  method[10000], path[10000], protocol[10000];
        char* file;
        size_t len;
        struct stat sb;
        if ( argc != 4 ) {
            printf("usage: ./micro_httpd absolute_path/ port num_worker\n");
            exit(-1);
            send_error( 500, "Internal Error", (char*) 0, "Config error - no dir specified." );
        }
        if ( chdir( argv[1] ) < 0 )
            send_error( 500, "Internal Error", (char*) 0, "Config error - couldn't chdir()." );
        strcpy(s_port_number,argv[2]);
        strcpy(s_num_worker,argv[3]);
        sscanf(s_port_number,"%d",&port_number);
        sscanf(s_num_worker,"%d",&num_worker);

        strcpy(method,"get");
        strcpy(path,argv[1]);
        strcpy(protocol,PROTOCOL);

        if ( strcasecmp( method, "get" ) != 0 )
            send_error( 501, "Not Implemented", (char*) 0, "That method is not implemented." );
        if ( path[0] != '/' )
            send_error( 400, "Bad Request", (char*) 0, "Bad filename." );
        file = &(path[0]);
        strdecode( file, file );
        if ( file[0] == '\0' )
            file = "./";
        len = strlen( file );
        if ( stat( file, &sb ) < 0 )
            send_error( 404, "Not Found", (char*) 0, "File not found." );

        int bossSocket, numWorker, clientSocket, notOverload, flagSignal;
        struct sockaddr_in serverAddr, clientAddr;
        socklen_t clientLen;
        in_port_t serverPort;
        pthread_t workerArr[MAX_WORKER];
        int threadId_arr[MAX_WORKER];

        serverPort = port_number;
        bossSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if ( bossSocket < 0 ) 
            print_system_error("socket() fails");

        memset(&serverAddr, 0, sizeof(struct sockaddr_in));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(serverPort);

        if ( bind(bossSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr) ) < 0 ) 
            print_system_error("bind() fails");

        clientQueue = create_queue();
        pthread_mutex_init(&queue_mutex, NULL);
        pthread_cond_init(&queue_has_client,NULL);

        numWorker = num_worker;
        create_worker(workerArr, threadId_arr,numWorker);

        if ( listen(bossSocket, MAX_WAIT) < 0 )
            print_system_error("listen() fails");

        while ( 1 ) {
            clientLen = sizeof(struct sockaddr_in);
            clientSocket = accept(bossSocket, (struct sockaddr *)&clientAddr, &clientLen);
            if ( clientSocket < 0 ) 
                print_system_error("accept error");
            pthread_mutex_lock(&queue_mutex);
            {
                flagSignal = 0;
                if ( is_empty_queue(clientQueue) ) 
                    flagSignal = 1;
                notOverload = enqueue(clientQueue, clientSocket);
                if ( flagSignal ) 
                    pthread_cond_broadcast(&queue_has_client);
            }
            pthread_mutex_unlock(&queue_mutex);
        }
    }
void create_worker(pthread_t * workerArr, int * threadId_arr, int numWorker)
{
    int threadIndex;
    int returnVal;
    char message[100];

    for(threadIndex = 0; threadIndex < numWorker; threadIndex++)
    {
        threadId_arr[threadIndex] = threadIndex;

        returnVal = pthread_create(&workerArr[threadIndex], NULL, handle_client_request, &threadId_arr[threadIndex]);
//        printf("Hi %d\n", threadIndex);
        if(returnVal)
        {
            sprintf(message, "pthread_create() fails with error code %d", returnVal); 
            print_system_error(message);
        }
    }

    printf("I am finished\n");
}

void * handle_client_request(void * workerId)
{
    int id = *((int *) workerId); 

    char * request = NULL;
    int clientSocket, parseSuccess;
    struct http_request_s * request_obj;
    struct http_response_s * response_obj;
    char *response = NULL;
//    printf("Hullo in %d\n", id);
    while(1)
    {
        clientSocket = -1;
        printf("Go to loop in %d\n", id);
        pthread_mutex_lock(&queue_mutex);
        {
            if(is_empty_queue(clientQueue))
            {
                pthread_cond_wait(&queue_has_client, &queue_mutex);
            }
            else 
                /* Take 1 client out of the queue */
                dequeue(clientQueue, &clientSocket);
        }
        pthread_mutex_unlock(&queue_mutex);
        printf("Worker with id = %d handles socketId = %d\n", id, clientSocket);
        if(clientSocket >= 0)
        {
            /* Initalize for new request handling */
            request_obj = create_request_struct();
            response_obj = create_response_struct();
            response = NULL;

            /* Handle the request of the client */
            request = read_request(clientSocket);
            /*                      ssize_t numByte = recv(clientSocket, m_request, 1000, 0);
                                    m_request[numByte] = '\0'; */
        //    printf("OK reading request\n"); 
            /* Parse request */
            parseSuccess = parse_http_request(request_obj, request, response_obj);

            response_obj->version = copy_str_dynamic(PROTOCOL);

            if(parseSuccess)
            {
                /* Check HTTP version */
                if(strcasecmp(request_obj->version, PROTOCOL) != 0)
                {
                    set_status_code_error(response_obj, 505, "505 HTTP Version Not Supported", "This server supports only HTTP/1.1");
                }
                else
                {
                    /* Check file access security */
                    if(count_occurence(request_obj->path, '/') > MAX_NUM_SLASH)
                        set_status_code_error(response_obj, 401, "401 Unauthorized", "You are not authorized to access the requested file on this server");
                    else
                    {
                        exec_http_request(request_obj, response_obj);
                    }
                }
            } 
            /* Execute command and return output 
               sprintf(response, "Server worker thread with id = %d handles request: %s", id, request); */

            response = get_response_text(response_obj);
            send(clientSocket, response, strlen(response), 0);

            //recv(clientSocket, response, 100, 0);
            /* Close socket, free memory */
            close(clientSocket);
            free(request);
            free(response);
            delete_request(&request_obj);
            delete_response(&response_obj); 
        }
    }
}

/* Here, "\r\n" signifies the end of a request */
char * read_request(int clientSocket)
{
    char buffer[MAX_LEN_REQUEST];
    int totalByte = 0;
    ssize_t numByteRecv, curBufferLen;
    char *curBuffer;

    curBuffer = buffer;
    curBufferLen = MAX_LEN_REQUEST;
    do
    {
        numByteRecv = recv(clientSocket, curBuffer, curBufferLen - 1, 0);
        printf("numByte = %d\n", numByteRecv);
        if(numByteRecv < 0)
            print_system_error("recv() fails");
        else
        { 
            /* Check if we reach the end of HTTP request */
            totalByte += (int) numByteRecv;
            if(totalByte >= 2 && buffer[totalByte - 1] == '\n' && buffer[totalByte - 2] == '\r')
            {
                buffer[totalByte] = '\0';
                break;
            }
            else
            {
                curBuffer = curBuffer + numByteRecv;
                curBufferLen -= (int) numByteRecv;
            }
        }
    } while(numByteRecv);

    buffer[totalByte] = '\0';

    return copy_str_dynamic(buffer);
}

    static void
file_details( char* dir, char* name )
{
    static char encoded_name[1000];
    static char path[2000];
    struct stat sb;
    char timestr[16];

    strencode( encoded_name, sizeof(encoded_name), name );
    (void) snprintf( path, sizeof(path), "%s/%s", dir, name );
    if ( lstat( path, &sb ) < 0 )
        (void) printf( "<a href=\"%s\">%-32.32s</a>    ???\n", encoded_name, name );
    else
    {
        (void) strftime( timestr, sizeof(timestr), "%d%b%Y %H:%M", localtime( &sb.st_mtime ) );
        (void) printf( "<a href=\"%s\">%-32.32s</a>    %15s %14lld\n", encoded_name, name, timestr, (long long) sb.st_size );
    }
}


static void
send_error( int status, char* title, char* extra_header, char* text )
    {
    send_headers( status, title, extra_header, "text/html", -1, -1 );
    (void) printf( "\
<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">\n\
<html>\n\
  <head>\n\
    <meta http-equiv=\"Content-type\" content=\"text/html;charset=UTF-8\">\n\
    <title>%d %s</title>\n\
  </head>\n\
  <body bgcolor=\"#cc9999\">\n\
    <h4>%d %s</h4>\n", status, title, status, title );
    (void) printf( "%s\n", text );
    (void) printf( "\
    <hr>\n\
    <address><a href=\"%s\">%s</a></address>\n\
  </body>\n\
</html>\n", SERVER_URL, SERVER_NAME );
    (void) fflush( stdout );
    exit( 1 );
    }


static void
send_headers( int status, char* title, char* extra_header, char* mime_type, off_t length, time_t mod )
    {
    time_t now;
    char timebuf[100];

    (void) printf( "%s %d %s\015\012", PROTOCOL, status, title );
    (void) printf( "Server: %s\015\012", SERVER_NAME );
    now = time( (time_t*) 0 );
    (void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &now ) );
    (void) printf( "Date: %s\015\012", timebuf );
    if ( extra_header != (char*) 0 )
	(void) printf( "%s\015\012", extra_header );
    if ( mime_type != (char*) 0 )
	(void) printf( "Content-Type: %s\015\012", mime_type );
    if ( length >= 0 )
	(void) printf( "Content-Length: %lld\015\012", (long long) length );
    if ( mod != (time_t) -1 )
	{
	(void) strftime( timebuf, sizeof(timebuf), RFC1123FMT, gmtime( &mod ) );
	(void) printf( "Last-Modified: %s\015\012", timebuf );
	}
    (void) printf( "Connection: close\015\012" );
    (void) printf( "\015\012" );
    }


static char*
get_mime_type( char* name )
    {
    char* dot;

    dot = strrchr( name, '.' );
    if ( dot == (char*) 0 )
	return "text/plain; charset=UTF-8";
    if ( strcmp( dot, ".html" ) == 0 || strcmp( dot, ".htm" ) == 0 )
	return "text/html; charset=UTF-8";
    if ( strcmp( dot, ".xhtml" ) == 0 || strcmp( dot, ".xht" ) == 0 )
	return "application/xhtml+xml; charset=UTF-8";
    if ( strcmp( dot, ".jpg" ) == 0 || strcmp( dot, ".jpeg" ) == 0 )
	return "image/jpeg";
    if ( strcmp( dot, ".gif" ) == 0 )
	return "image/gif";
    if ( strcmp( dot, ".png" ) == 0 )
	return "image/png";
    if ( strcmp( dot, ".css" ) == 0 )
	return "text/css";
    if ( strcmp( dot, ".xml" ) == 0 || strcmp( dot, ".xsl" ) == 0 )
	return "text/xml; charset=UTF-8";
    if ( strcmp( dot, ".au" ) == 0 )
	return "audio/basic";
    if ( strcmp( dot, ".wav" ) == 0 )
	return "audio/wav";
    if ( strcmp( dot, ".avi" ) == 0 )
	return "video/x-msvideo";
    if ( strcmp( dot, ".mov" ) == 0 || strcmp( dot, ".qt" ) == 0 )
	return "video/quicktime";
    if ( strcmp( dot, ".mpeg" ) == 0 || strcmp( dot, ".mpe" ) == 0 )
	return "video/mpeg";
    if ( strcmp( dot, ".vrml" ) == 0 || strcmp( dot, ".wrl" ) == 0 )
	return "model/vrml";
    if ( strcmp( dot, ".midi" ) == 0 || strcmp( dot, ".mid" ) == 0 )
	return "audio/midi";
    if ( strcmp( dot, ".mp3" ) == 0 )
	return "audio/mpeg";
    if ( strcmp( dot, ".ogg" ) == 0 )
	return "application/ogg";
    if ( strcmp( dot, ".pac" ) == 0 )
	return "application/x-ns-proxy-autoconfig";
    return "text/plain; charset=UTF-8";
    }


static void
strdecode( char* to, char* from )
    {
    for ( ; *from != '\0'; ++to, ++from )
	{
	if ( from[0] == '%' && isxdigit( from[1] ) && isxdigit( from[2] ) )
	    {
	    *to = hexit( from[1] ) * 16 + hexit( from[2] );
	    from += 2;
	    }
	else
	    *to = *from;
	}
    *to = '\0';
    }


static int
hexit( char c )
    {
    if ( c >= '0' && c <= '9' )
	return c - '0';
    if ( c >= 'a' && c <= 'f' )
	return c - 'a' + 10;
    if ( c >= 'A' && c <= 'F' )
	return c - 'A' + 10;
    return 0;		/* shouldn't happen, we're guarded by isxdigit() */
    }


static void
strencode( char* to, size_t tosize, const char* from )
    {
    int tolen;

    for ( tolen = 0; *from != '\0' && tolen + 4 < tosize; ++from )
	{
	if ( isalnum(*from) || strchr( "/_.-~", *from ) != (char*) 0 )
	    {
	    *to = *from;
	    ++to;
	    ++tolen;
	    }
	else
	    {
	    (void) sprintf( to, "%%%02x", (int) *from & 0xff );
	    to += 3;
	    tolen += 3;
	    }
	}
    *to = '\0';
    }
