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
  GET,
  POST
};

struct httpResponse {
  int statusCode;
  char *reasonPhrase;
  char *contentBuff;
  int contentSize;
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

// crafts response header with stat line and content
// returns buff used mem size
int craftResp(struct httpResponse *resp, char *respBuff, int respBuffSize) {
  if (resp->statusCode < 100 || resp->statusCode > 511) {
    printf("status line resp stat code invlid %i \n", resp->statusCode);
    return 1;
  }
  char tempBuff[100] = {};
  int tempSize = 0;

  // status line
  tempSize = sprintf(tempBuff, "HTTP/%s %d %s", HTTP_VERSION, resp->statusCode, resp->reasonPhrase);
  strncat(respBuff, tempBuff, tempSize);
  strncat(respBuff, "\r\n", 2);

  // response header
  tempSize = sprintf(tempBuff, "Server: basicWebserver/%s", WS_VERSION);
  strncat(respBuff, tempBuff, tempSize);
  strncat(respBuff, "\r\n", 2);

  // entity header
  tempSize = sprintf(tempBuff, "Content-type: text/html, text, plain");
  strncat(respBuff, tempBuff, tempSize);
  strncat(respBuff, "\r\n", 2);

  tempSize = sprintf(tempBuff, "Content-length: %d", resp->contentSize);
  strncat(respBuff, tempBuff, tempSize);
  strncat(respBuff, "\r\n", 2);
  strncat(respBuff, "\r\n", 2);

  strncat(respBuff, resp->contentBuff, resp->contentSize);

  return 0;
}

int parseHttpRequest(struct httpRequest *req, char *reqBuff, int reqBuffSize) {
  #ifdef DEBUG
  printf("------------ request -------------\n");
  printf("%s \n", reqBuff);
  printf("------------ request -------------\n");
  #endif

  const int tempBuffSize = 100;
  char tempBuff[tempBuffSize] = {};
  char *tok;
  int iElement = 0;
  int iElementSize = 0;
  int iElementUsedMem = 0;

  for (int i = 0; i<reqBuffSize; i++) {
    // request line parsing
    if (reqBuff[i] == SP || (reqBuff[i] == CR && reqBuff[i+1] == LF)) {
      if (iElement == 0) {
        iElementSize = i;
      } else {
        iElementSize = (i-iElementUsedMem);
      }
      if (iElementSize > tempBuffSize) {
        printf("http req parser buff size exceeded \n");
        return 1;
      }

      memcpy(tempBuff, reqBuff+iElementUsedMem, iElementSize);

      switch (iElement) {
        case 0:
          if (strncmp(tempBuff, "GET", iElementSize) == 0) {
            req->reqMethod = GET;
          } else if (strncmp(tempBuff, "POST", iElementSize) == 0) {
            req->reqMethod = POST;
          }
        case 1:
          req->requestUri = (char*)malloc(iElementSize);
          memcpy(req->requestUri, tempBuff, iElementSize);
        case 2:
          tok = strtok(tempBuff, "/");
          tok = strtok(NULL, "/");
          printf("sadas: %s \n", tok);
      }

      iElementUsedMem += iElementSize;
      iElement++;
      if (reqBuff[i] == CR && reqBuff[i+1] == LF ) {
        break;
      }
    }
  }
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
  struct pthreadClientHandleArgs *argss = (struct pthreadClientHandleArgs*)args;
  char *readBuff = (char*)malloc(WS_BUFF_SIZE);
  char *respBuff = (char*)malloc(WS_BUFF_SIZE);
  struct httpRequest httpReq = {};
  struct httpResponse resp = {};
  int rc;

  int readBuffSize = read(argss->socket, readBuff, WS_BUFF_SIZE);

  // sec checks
  if (readBuffSize >= WS_BUFF_SIZE) {
    resp.statusCode = 500;
    resp.reasonPhrase = "err";
    resp.contentBuff = "http req exceeds buffer size";
    resp.contentSize = strlen(resp.contentBuff);
    printf("http req exceeds defined buffer size \n");
    pthread_exit(NULL);
  }
  if (readBuff[readBuffSize] != (char)0) {
    resp.statusCode = 500;
    resp.reasonPhrase = "err";
    resp.contentBuff = "http req invalid";
    resp.contentSize = strlen(resp.contentBuff);
    printf("http req invalid \n");
    pthread_exit(NULL);
  }

  rc = parseHttpRequest(&httpReq, readBuff, readBuffSize);
  if (rc != 0){
    resp.statusCode = 500;
    resp.reasonPhrase = "err";
    resp.contentBuff = "http req parsing failed";
    resp.contentSize = strlen(resp.contentBuff);
    printf("http req parsing failed \n");
    pthread_exit(NULL);
  }

  #ifdef DEBUG
  printf("------------ parsed request -------------\n");
  printf("http version: %f \n", httpReq.httpVersion);
  printf("req method: %i \n", httpReq.reqMethod);
  printf("req uri: %s \n", httpReq.requestUri);
  printf("------------ parsed request -------------\n");
  #endif

  resp.statusCode = 200;
  resp.reasonPhrase = "ok";
  resp.contentBuff = "test test";
  resp.contentSize = strlen(resp.contentBuff);

  rc = craftResp(&resp, respBuff, WS_BUFF_SIZE);
  if (rc == -1){
    printf("craft Status Line Resp err \n");
    pthread_exit(NULL);
  }

  #ifdef DEBUG
  printf("------------ response -------------\n");
  printfBuffer(respBuff, strlen(respBuff));
  printf("------------ response -------------\n");
  fflush(stdout);
  #endif

  rc = sendBuffer(argss->socket, respBuff, strlen(respBuff));
  if (rc != 0) {
    printf("send Buffer err \n");
    pthread_exit(NULL);
  }

  free(readBuff);
  free(respBuff);
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
