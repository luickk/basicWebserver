#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

// #define DEBUG 1

#define LOG_STREAM stdout

#define MAX_CLIENTS 10

#define MAX_HTTP_METHOD_LEN 10

#define MAX_ERR_REASON_LEN 100
#define MAX_ERR_PREFIX_LEN 40

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

  struct httpRoute **routes;
  int nRoutes;
} webserver;

typedef struct {
  char prefix[MAX_ERR_PREFIX_LEN];
  char reason[MAX_ERR_REASON_LEN];
  int rc;
} wsError;

struct pthreadClientHandleArgs {
  webserver *wserver;
  int socket;
};

struct httpRoute {
  const char *path;
  const char *method;
  struct httpResponse *httpResp;
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
};

// declares&inits error struct and assigns the prefix to the struct
// returns err struct ref
wsError* initWsError(char prefix[MAX_ERR_PREFIX_LEN]) {
  wsError *err = (wsError*)malloc(sizeof(wsError));
  strcpy(err->prefix, prefix);
  return err;
}

// sets passed error struct reason attr to char arr
// supports format
// returns modified err struct reference
void setErr(wsError *err, const char* format, ...) {
  va_list argptr;
  va_start(argptr, format);
  vsprintf(err->reason, format, argptr);
  va_end(argptr);

  // assert(strlen(err->reason) >= MAX_ERR_REASON_LEN);
  err->rc = 1;
}

// prints referenced error struct prefix+reason
void printErr(wsError *err) {
  fprintf(stderr, "%s %s", err->prefix, err->reason);
}

// printf's log char arr to given stream
// supports format
void wsLog(const char* format, ...) {
  va_list argptr;
  va_start(argptr, format);
  fprintf(LOG_STREAM, format, argptr);
  va_end(argptr);
  fflush(stdout);
}

// declares&inits route struct
// defines route struct attr
// returns reference to route struct
struct httpRoute *createRoute(const char *path, const char *method, struct httpResponse *resp) {
  struct httpRoute *route = (struct httpRoute*)malloc(sizeof(struct httpRoute));
  route->path = path;
  route->method = method;
  route->httpResp = resp;

  return route;
}

// frees all route structs attributes and struct itself from webserver struct
void freeRoutes(webserver *ws) {
  for (int i = 0; i < ws->nRoutes; i++) {
    free(ws->routes[i]->httpResp);
    free(ws->routes[i]);
  }
  free(ws->routes);
}

// adds route struct reference to webserver routes pointer arr
// dynamically re/allocates memory
void addRouteToWs(webserver *ws, struct httpRoute *route) {
  if (ws->nRoutes == 0) {
    ws->routes = (struct httpRoute**)malloc(sizeof(struct httpRoute**));
    ws->routes[ws->nRoutes] = route;
  } else {
    ws->routes = (struct httpRoute**)realloc(ws->routes, (ws->nRoutes+1)*sizeof(struct httpRoute**));
    ws->routes[ws->nRoutes] = route;
  }
  ws->nRoutes++;
}

// removes space characters from given string ref
void removeSpaces(char* str, int strlen) {
  int count = 0;
  for (int i = 0; i <= strlen; i++)
      if (str[i] != ' ')
          str[count++] = str[i];
}

// sends buffer on given socket
// returns sent data size
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

// prints & flushes buffer to stdout
void printfBuffer(char *buff, int buffSize) {
  fwrite(buff, buffSize, 1, stdout);
  fflush(stdout);
}

// by https://stackoverflow.com/a/3464656, modified
int readFileToBuffer(char *filename, char *buffer, int bufferSize, wsError *err) {
   int stringSize, readSize;
   FILE *handler = fopen(filename, "r");

   if (handler) {
       fseek(handler, 0, SEEK_END);
       stringSize = ftell(handler);
       rewind(handler);

       if (stringSize > bufferSize)
         setErr(err, "read file exceeds buffer size \n");
         return 0;

       readSize = fread(buffer, sizeof(char), stringSize, handler);
       buffer[stringSize] = '\0';

       if (stringSize != readSize)
         setErr(err, "file read sizes conflict \n");
         return 0;

       // always remember to close the file
       fclose(handler);
    } else {
      setErr(err, "file open error (file not found?) \n");
      return 0;
    }
    err->rc = 0;
    return readSize;
  }

// crafts response with stat line, entity header and content from httpResponse struct
// puts crafted response into the respBuff
// returns respBuff size
int craftResp(struct httpResponse *resp, char *respBuff, int respBuffSize, wsError *err) {
  if (resp->statusCode < 100 || resp->statusCode > 511) {
    setErr(err, "status line resp stat code invlid %i \n", resp->statusCode);
    return 0;
  }
  int size = 0;

  // status line
  size += sprintf(respBuff+size, "HTTP/%s %d %s", HTTP_VERSION, resp->statusCode, resp->reasonPhrase);
  size += sprintf(respBuff+size, "\r\n");

  // entity header
  size += sprintf(respBuff+size, "Content-type: text/html, text, plain");
  size += sprintf(respBuff+size, "\r\n");

  size += sprintf(respBuff+size, "Content-length: %d", resp->contentSize);
  size += sprintf(respBuff+size, "\r\n");

  // content
  size += sprintf(respBuff+size, "\r\n");
  strncat(respBuff, resp->contentBuff, resp->contentSize);

  err->rc = 0;
  return size;
}

// parses incoming httpRequest from reqBuff to the httpRequest struct
// returns reqBuffSize size
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
          removeSpaces(req->requestUri, iElementSize);
          break;
        case 2:
          // extracing version number - http/x.x
          // strtok directly used on reqBuff since it is the last parsed element
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

  err->rc = 0;
  return iElementUsedMem;
}

// inits the webserver struct on given port
// binds & starts listening on webserver Socket
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

  err->rc = 0;
}

// frees all allocated memory from the clientHandle thread
void freeClient(char *readBuff, char *respBuff, struct httpRequest *httpReq, wsError *err) {
  free(readBuff);
  free(respBuff);
  free(httpReq->requestUri);
  free(httpReq->reqMethod);
  free(err);
}

// the clientHandle thread, waits for incoming request and delivers the reply accordingly
// quits thread after reply and does not continue to reply to multiple requests on one connection. does not support persistent connections
void *clientHandle(void *args) {
  struct pthreadClientHandleArgs *argss = (struct pthreadClientHandleArgs*)args;
  wsError *err = initWsError("error(clientHandle)->");

  char *readBuff = (char*)malloc(WS_BUFF_SIZE);
  char *respBuff = (char*)malloc(WS_BUFF_SIZE);
  struct httpRequest httpReq = {};
  struct httpResponse resp = {};
  int respSize, reqSize, sentBuffSize;
  int routeFound = 0;

  wsLog("new client thread created \n");

  int readBuffSize = read(argss->socket, readBuff, WS_BUFF_SIZE);

  // sec checks
  if (readBuffSize >= WS_BUFF_SIZE) {
    setErr(err, "http req exceeds buffer size \n");
    printErr(err);
    pthread_exit(NULL);
  }
  if (readBuff[readBuffSize] != (char)0) {
    setErr(err, "http req invalid \n");
    printErr(err);
    freeClient(readBuff, respBuff, &httpReq, err);
    close(argss->socket);
    pthread_exit(NULL);
  }

  reqSize = parseHttpRequest(&httpReq, readBuff, readBuffSize, err);
  if (err->rc != 0){
    printErr(err);
    freeClient(readBuff, respBuff, &httpReq, err);
    close(argss->socket);
    pthread_exit(NULL);
  }

  #ifdef DEBUG
  printf("------------ parsed request -------------\n");
  printf("http version: %f \n", httpReq.httpVersion);
  printf("req method: %s \n", httpReq.reqMethod);
  printf("req uri: %s \n", httpReq.requestUri);
  printf("------------ parsed request -------------\n");
  #endif

  pthread_mutex_lock(&argss->wserver->mutexLock);
  for (int i = 0; i < argss->wserver->nRoutes; i++) {
    if (strcmp(argss->wserver->routes[i]->path, httpReq.requestUri) == 0) {
      routeFound = 1;
      respSize = craftResp(argss->wserver->routes[i]->httpResp, respBuff, WS_BUFF_SIZE, err);
      if (err->rc != 0){
        printErr(err);
        freeClient(readBuff, respBuff, &httpReq, err);
        close(argss->socket);
        pthread_exit(NULL);
      }

      sentBuffSize = sendBuffer(argss->socket, respBuff, strlen(respBuff), err);
      if (err->rc != 0) {
        printErr(err);
        freeClient(readBuff, respBuff, &httpReq, err);
        close(argss->socket);
        pthread_exit(NULL);
      }
    }
  }
  pthread_mutex_unlock(&argss->wserver->mutexLock);

  if (!routeFound) {
    setErr(err, "page not found");
    resp.statusCode = 404;
    resp.reasonPhrase = "err";
    resp.contentBuff = err->reason;
    resp.contentSize = strlen(resp.contentBuff);
    respSize = craftResp(&resp, respBuff, WS_BUFF_SIZE, err);
    if (err->rc != 0){
      printErr(err);
      freeClient(readBuff, respBuff, &httpReq, err);
      close(argss->socket);
      pthread_exit(NULL);
    }

    sentBuffSize = sendBuffer(argss->socket, respBuff, strlen(respBuff), err);
    if (err->rc != 0) {
      printErr(err);
      freeClient(readBuff, respBuff, &httpReq, err);
      close(argss->socket);
      pthread_exit(NULL);
    }
  }

  #ifdef DEBUG
  printf("------------ response -------------\n");
  printf(respBuff, strlen(respBuff));
  printf("------------ response -------------\n");
  fflush(stdout);
  #endif

  wsLog("server-response sent \n");

  freeClient(readBuff, respBuff, &httpReq, err);
  close(argss->socket);
  pthread_exit(NULL);
}

// waits for new incoming connections on port and creates clientHandles threads accordingly
void wsListen(webserver *wserver, wsError *err) {
  struct sockaddr_in tempClient;
  struct pthreadClientHandleArgs *clientArgs = (struct pthreadClientHandleArgs *)malloc(sizeof(struct pthreadClientHandleArgs));

  if (pthread_mutex_init(&wserver->mutexLock, NULL) != 0) {
    setErr(err, "mutex init failed \n");
    return;
  }

  wsLog("server listening \n");

  int newSocket;
  socklen_t addr_size;
  while(1)
  {
    addr_size = sizeof tempClient;
    newSocket = accept(wserver->wserverSocket, (struct sockaddr *) &tempClient, &addr_size);
    wsLog("new client connected \n");

    #ifdef DEBUG
    // telling the kernel to that the socket is reused - only for debugging purposes
    int yes=1;
    if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        setErr(err, "(debug flag set) socket reuse set failed \n");
        return;
    }
    #endif

    clientArgs->wserver = wserver;

    clientArgs->socket = newSocket;
    if(pthread_create(&wserver->clientThreads[wserver->clientIdThreadCounter], NULL, clientHandle, (void*)clientArgs) != 0 ) {
      setErr(err, "thread create error \n");
      free(clientArgs);
      return;
    } else {
      wserver->clientIdThreadCounter++;
    }
  }
  free(clientArgs);
  err->rc = 0;
}

// frees the webserver struct and all allocated attributes
void freeWs(webserver *wserver) {
  freeRoutes(wserver);
  free(wserver);
}

/*
 * Server Main.
 */
int main() {
  wsError *err = initWsError("error(server)->");
  webserver *wserver = (webserver *)malloc(sizeof(webserver));

  wsInit(wserver, 80, err);
  if (err->rc != 0) {
    printErr(err);
    return 1;
  }
  wsLog("server initiated \n");

  struct httpResponse *mainRouteResponse = (struct httpResponse*)malloc(sizeof(struct httpResponse));
  mainRouteResponse->statusCode = 200;
  mainRouteResponse->reasonPhrase = "succ";
  mainRouteResponse->contentBuff = "Hai";
  mainRouteResponse->contentSize = 3;
  struct httpRoute *mainRoute = createRoute("/", "GET", mainRouteResponse);
  addRouteToWs(wserver, mainRoute);

  struct httpResponse *routeResponse = (struct httpResponse*)malloc(sizeof(struct httpResponse));
  routeResponse->statusCode = 200;
  routeResponse->reasonPhrase = "succ";
  routeResponse->contentBuff = "lol";
  routeResponse->contentSize = 3;
  struct httpRoute *route = createRoute("/lol", "GET", routeResponse);
  addRouteToWs(wserver, route);

  wsListen(wserver, err);
  if (err->rc != 0) {
    printErr(err);

    freeWs(wserver);
    free(err);
    return 1;
  }

  freeWs(wserver);
  free(err);
  return 0;
}
