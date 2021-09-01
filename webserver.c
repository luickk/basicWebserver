#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define DEBUG 1

#define MAX_CLIENTS 10

#define CR 0x0D
#define LF 0x0A
#define SP 0x20

#define REQ_LINE_LEN 3

#define HTTP_VERSION "1.1"
#define WS_VERSION "1.0"

#define WS_BUFF_SIZE 1024

typedef struct {
  unsigned short port;
  struct sockaddr_in server;
  int wserverSocket;
  pthread_t clientThreads[MAX_CLIENTS];
  int clientIdThreadCounter;
  pthread_mutex_t mutexLock;
} webserver;

struct pthreadClientHandleArgs{
  webserver *wserver;
  int socket;
};

enum httpMethod {
  OPTIONS,
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  TRACE,
  CONNECT
};

struct httpRequest {
  int reqMethod;
  double httpVersion;
  char *requestUri;

  // char *browserAccept;
  // char *browserUserAgent;
  // char *browserAccEncoding;
  // char *accLang;
  // char *connType;
};

int sendBuffer(int sock, char *buff, int buffSize) {
  int sendLeft = buffSize;
  int rc;
  while (sendLeft > 0)
  {
    rc = send(sock, buff+(buffSize-sendLeft), sendLeft, 0);
    if (rc == -1) {
     return 1;
    }
    sendLeft -= rc;
  }
  return 0;
}

void printfBuffer(char *buff, int buffSize) {
  fwrite(buff, buffSize, 1, stdout);
  fflush(stdout);
}

int craftResp(int statusCode, char *reasonPhrase, char *contentBuff, int contentSize,
              char *respBuff, int *respSize) {
  if (statusCode < 100 || statusCode > 500) {
    printf("status line resp stat code invlid");
    return 1;
  }

  // status line
  *respSize += sprintf(respBuff+*respSize, "HTTP/%s %d %s", HTTP_VERSION, statusCode, reasonPhrase);
  // CRLF
  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  // response header
  *respSize += sprintf(respBuff+*respSize, "Server: basicWebserver/%s", WS_VERSION);
  // CRLF
  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  // entity header
  *respSize += sprintf(respBuff+*respSize, "Content-type: text/html, text, plain");
  // CRLF
  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  *respSize += sprintf(respBuff+*respSize, "Content-length: %d", contentSize);
  // CRLF
  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  strncat(respBuff+*respSize, contentBuff, contentSize);
  *respSize += contentSize;

  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  respBuff[*respSize+1] = CR;
  respBuff[*respSize+2] = LF;
  *respSize += 2;

  // printf("%s \n", respBuff);
  // printf("%d \n", *respSize);

  return 0;
}

int parseHttpRequest(char reqBuff[], struct httpRequest *req, int reqBuffSize) {
  char *reqBuffCpy = strdup(reqBuff);

  char **reqLineElements = (char**)malloc(REQ_LINE_LEN);
  int iELement = 0;

  for (int i = 0; i<reqBuffSize; i++) {
    // request line parsing
    if (reqBuffCpy[i] == SP || (reqBuffCpy[i] == CR && reqBuffCpy[i+1] == LF)) {
      reqLineElements[iELement] = (char*)malloc(i+1);
      strncpy(reqLineElements[iELement], reqBuffCpy, i);
      reqLineElements[iELement][i+1] = (char)0;
      iELement++;
      if (reqBuffCpy[i] == CR && reqBuffCpy[i+1] == LF ) {
        break;
      }
    }
  }

  free(reqBuffCpy);
  return 0;
}

int wsInit(webserver *wserver, int port) {
  wserver->port = port;

  if ((wserver->wserverSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("socket err \n");
    return 1;
  }

  wserver->server.sin_family = AF_INET;
  wserver->server.sin_port = htons(port);
  wserver->server.sin_addr.s_addr = INADDR_ANY;

  wserver->mutexLock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  wserver->clientIdThreadCounter = 0;

  if (bind(wserver->wserverSocket, (struct sockaddr *)&wserver->server, sizeof(wserver->server)) < 0)
  {
    printf("bind err \n");
    return 1;
  }

  if (listen(wserver->wserverSocket, 1) != 0)
  {
    printf("listenerr \n");
    return 1;
  }
  return 0;
}

void *clientHandle(void *args) {
  struct httpRequest *httpReq = (struct httpRequest*)malloc(sizeof(struct httpRequest));
  struct pthreadClientHandleArgs *argss = (struct pthreadClientHandleArgs*)args;
  char buff[WS_BUFF_SIZE] = {};
  int rc, *respSize;

  rc = craftResp(400, "err", "p", 1, buff, &*respSize);
  if (rc != 0){
    printf("craft Status Line Resp err \n");
    pthread_exit(NULL);
  }
  printfBuffer(buff, WS_BUFF_SIZE);
  // write(argss->socket, buff, sizeof(buff));
  // sendBuffer(argss->socket, buff, WS_BUFF_SIZE);

  int buffSize = read(argss->socket, buff, sizeof(buff));

  // sec checks
  if (buffSize >= WS_BUFF_SIZE) {
    printf("http req exceeds defined buffer size \n");
    pthread_exit(NULL);
  }
  if (buff[buffSize] != (char)0) {
    printf("http req invalid \n");
    pthread_exit(NULL);
  }

  // printf("From client: %s \n ", buff);
  rc = parseHttpRequest(buff, httpReq, buffSize);
  if (rc != 0){
    printf("http req parsing failed \n");
    pthread_exit(NULL);
  }


  printf("http version: %f \n", httpReq->httpVersion);
  printf("req method: %i \n", httpReq->reqMethod);
  printf("req uri: %s \n", httpReq->requestUri);

  fflush(stdout);
  pthread_exit(NULL);
}

int wsListen(webserver *wserver) {
  struct sockaddr_in tempClient;
  static struct pthreadClientHandleArgs clientArgs = {};

  int newSocket;
  socklen_t addr_size;
  while(1)
  {
    addr_size = sizeof tempClient;
    newSocket = accept(wserver->wserverSocket, (struct sockaddr *) &tempClient, &addr_size);

    #ifdef DEBUG
    // telling the kernel to that the socket is reused - only for debugging purposes
    int yes=1;
    if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt");
        return 1;
    }
    #endif

    clientArgs.wserver = wserver;
    clientArgs.socket = newSocket;
    if(pthread_create(&wserver->clientThreads[wserver->clientIdThreadCounter], NULL, clientHandle, (void*)&clientArgs) != 0 ) {
      return 1;
    } else {
      wserver->clientIdThreadCounter++;
    }

  }
  return 0;
}

/*
 * Server Main.
 */
int main() {
  int rc = 0;
  webserver *wserver = (webserver *)malloc(sizeof(webserver));

  rc = wsInit(wserver, 80);
  if (rc != 0) {
    printf("failed to init ws struct \n");
    return 1;
  }

  rc = wsListen(wserver);
  if (rc != 0) {
    printf("failed to setup client conn \n");
    return 1;
  }

  free(wserver);
  printf("Server ended successfully \n");
  return 0;
}
