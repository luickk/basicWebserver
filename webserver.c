#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

#define DEBUG 1

#define MAX_CLIENTS 10

#define MAX_HTTP_METHOD_LEN 10

#define MAX_ERR_REASON_LEN 100
#define MAX_ERR_PREFIX_LEN 20

#define CR 0x0D
#define LF 0x0A
#define SP 0x20

#define HTTP_REQ_LINE_LEN 3

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

typedef struct {
  char prefix[MAX_ERR_PREFIX_LEN];
  char reason[MAX_ERR_REASON_LEN];
  int rc;
} wsError;

struct pthreadClientHandleArgs{
  webserver *wserver;
  int socket;
};

char httpMethods[][MAX_HTTP_METHOD_LEN] = { "GET", "POST" };

struct httpResponse {
  int statusCode;
  char *reasonPhrase;
  char *contentBuff;
  int contentSize;
};

struct httpRequest {
  char *reqMethod;
  float httpVersion;
  char *requestUri;

  // char *browserAccept;
  // char *browserUserAgent;
  // char *browserAccEncoding;
  // char *accLang;
  // char *connType;
};

wsError* initWsError(char prefix[MAX_ERR_PREFIX_LEN]) {
  wsError *err = (wsError*)malloc(sizeof(char)*(MAX_ERR_REASON_LEN+MAX_ERR_PREFIX_LEN)+sizeof(int));
  strcpy(err->prefix, prefix);
  return err;
}

void setErr(wsError *err, const char* format, ...) {
  va_list argptr;
  va_start(argptr, format);
  vsprintf(err->reason, format, argptr);
  va_end(argptr);

  assert(strlen(err->reason) >= MAX_ERR_REASON_LEN);
  err->rc = 1;
}
void printErr(wsError *err) {
  fprintf(stderr, "%s %s", err->prefix, err->reason);
}

int sendBuffer(int sock, char *buff, int buffSize, wsError *err) {
  int sendLeft = buffSize;
  int dataSent = 0;
  int rc;
  while (sendLeft > 0)
  {
    rc = send(sock, buff+(buffSize-sendLeft), sendLeft, 0);
    if (rc == -1) {
      setErr(err, "send failed \n");
      return 0;
    }
    sendLeft -= rc;
    dataSent += rc;
  }
  err->rc = 0;
  return dataSent;
}

void printfBuffer(char *buff, int buffSize) {
  fwrite(buff, buffSize, 1, stdout);
  fflush(stdout);
}

// crafts response header with stat line and content
// returns buff used mem size
int craftResp(struct httpResponse *resp, char *respBuff, int respBuffSize, wsError *err) {
  if (resp->statusCode < 100 || resp->statusCode > 511) {
    setErr(err, "status line resp stat code invlid %i \n", resp->statusCode);
    return 0;
  }
  char tempBuff[100] = {};
  int tempSize = 0;
  int size = 0;

  for (int i = 0; i<4; i++) {
    switch (i){
      case 0:
        // status line
        tempSize = sprintf(tempBuff, "HTTP/%s %d %s", HTTP_VERSION, resp->statusCode, resp->reasonPhrase);
        break;
      case 1:
        // response header
        tempSize = sprintf(tempBuff, "Server: basicWebserver/%s", WS_VERSION);
        break;
      case 2:
        // entity header
        tempSize = sprintf(tempBuff, "Content-type: text/html, text, plain");
        break;
      case 3:
        tempSize = sprintf(tempBuff, "Content-length: %d", resp->contentSize);
        break;
    }
    strncat(respBuff, tempBuff, tempSize);
    strncat(respBuff, "\r\n", 2);
    size += tempSize+2;
  }
  // content
  strncat(respBuff, "\r\n", 2);
  strncat(respBuff, resp->contentBuff, resp->contentSize);
  size += resp->contentSize+2;

  return size;
}

int parseHttpRequest(struct httpRequest *req, char *reqBuff, int reqBuffSize, wsError *err) {
  #ifdef DEBUG
  printf("------------ request -------------\n");
  printf("%s \n", reqBuff);
  printf("------------ request -------------\n");
  #endif

  char *tok;
  int endOfParse = 0;

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
      switch (iElement) {
        case 0:
          req->reqMethod = (char*)malloc(iElementSize);
          memcpy(req->reqMethod, reqBuff+iElementUsedMem, iElementSize);
          for (int i = 0; i<(sizeof(httpMethods)/MAX_HTTP_METHOD_LEN); i++) {
            if (strcmp(req->reqMethod, httpMethods[i]) == 0)
              break;
            if (i == (sizeof(httpMethods)/MAX_HTTP_METHOD_LEN))
              setErr(err, "http method not supported \n");
              return 0;
          }
          break;
        case 1:
          req->requestUri = (char*)malloc(iElementSize);
          memcpy(req->requestUri, reqBuff+iElementUsedMem, iElementSize);
          break;
        case 2:
          // extracing version number - http/x.x
          tok = strtok(reqBuff+iElementUsedMem, "/");
          tok = strtok(NULL, "/");
          req->httpVersion = atof(tok);
          endOfParse = 1;
          break;
      }
      iElementUsedMem += iElementSize;
      iElement++;
      if (endOfParse)
        break;
    }
  }
  return iElementUsedMem;
}

void wsInit(webserver *wserver, int port, wsError *err) {
  wserver->port = port;

  if ((wserver->wserverSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    setErr(err, "socket err \n");
    return;
  }

  wserver->server.sin_family = AF_INET;
  wserver->server.sin_port = htons(port);
  wserver->server.sin_addr.s_addr = INADDR_ANY;

  wserver->mutexLock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  wserver->clientIdThreadCounter = 0;

  if (bind(wserver->wserverSocket, (struct sockaddr *)&wserver->server, sizeof(wserver->server)) < 0)
  {
    setErr(err, "http server socket binding err \n");
    return;
  }

  if (listen(wserver->wserverSocket, 1) != 0)
  {
    setErr(err, "http server init listen err \n");
    return;
  }
}

void *clientHandle(void *args) {
  struct pthreadClientHandleArgs *argss = (struct pthreadClientHandleArgs*)args;
  wsError *err = initWsError("clientHandle->");

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

  rc = parseHttpRequest(&httpReq, readBuff, readBuffSize, err);
  if (err->rc != 0){
    printErr(err);
    resp.statusCode = 500;
    resp.reasonPhrase = "err";
    resp.contentBuff = err->reason;
    resp.contentSize = strlen(resp.contentBuff);
    pthread_exit(NULL);
  }

  #ifdef DEBUG
  printf("------------ parsed request -------------\n");
  printf("http version: %f \n", httpReq.httpVersion);
  printf("req method: %s \n", httpReq.reqMethod);
  printf("req uri: %s \n", httpReq.requestUri);
  printf("------------ parsed request -------------\n");
  #endif

  resp.statusCode = 200;
  resp.reasonPhrase = "ok";
  resp.contentBuff = "test test";
  resp.contentSize = strlen(resp.contentBuff);

  rc = craftResp(&resp, respBuff, WS_BUFF_SIZE, err);
  if (err->rc != 0){
    printErr(err);
    pthread_exit(NULL);
  }

  #ifdef DEBUG
  printf("------------ response -------------\n");
  printfBuffer(respBuff, strlen(respBuff));
  printf("------------ response -------------\n");
  fflush(stdout);
  #endif

  rc = sendBuffer(argss->socket, respBuff, strlen(respBuff), err);
  if (err->rc != 0) {
    printErr(err);
    pthread_exit(NULL);
  }

  free(readBuff);
  free(respBuff);
  free(err);
  pthread_exit(NULL);
}

void wsListen(webserver *wserver, wsError *err) {
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
        setErr(err, "(debug enabled) socket reuse set failed \n");
        return;
    }
    #endif

    clientArgs.wserver = wserver;
    clientArgs.socket = newSocket;
    if(pthread_create(&wserver->clientThreads[wserver->clientIdThreadCounter], NULL, clientHandle, (void*)&clientArgs) != 0 ) {
      setErr(err, "thread create error \n");
      return;
    } else {
      wserver->clientIdThreadCounter++;
    }
  }
}

/*
 * Server Main.
 */
int main() {
  int rc = 0;
  wsError *err = initWsError("server->");
  webserver *wserver = (webserver *)malloc(sizeof(webserver));

  wsInit(wserver, 80, err);
  if (err->rc != 0) {
    printErr(err);
    return 1;
  }

  wsListen(wserver, err);
  if (err->rc != 0) {
    printErr(err);
    return 1;
  }

  free(wserver);
  free(err);
  printf("Server stopped \n");
  return 0;
}
