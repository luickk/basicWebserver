 #include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

// #define DEBUG 1

// defines version contained within the client reply
#define HTTP_VERSION "1.0"

// defines used logstream
#define LOG_STREAM stdout

// client request buffer size
#define WS_BUFF_SIZE 1024

// logging and static len vars
#define MAX_HTTP_METHOD_LEN 10
#define MAX_ERR_REASON_LEN 100
#define MAX_ERR_PREFIX_LEN 40

// important string parsing literals
#define CR 0x0D
#define LF 0x0A
#define SP 0x20

// parsing consts
#define HTTP_REQ_LINE_LEN 3

struct clientThreadState {
  struct pthreadClientHandleArgs *args;
  pthread_t *clientThread;
};

typedef struct {
  struct sockaddr_in server;
  struct clientThreadState *clientThreads;
  struct httpRoute **routes;

  int wserverSocket;
  int clientIdThreadCounter;
  int nRoutes;

  pthread_mutex_t mutexLock;
  unsigned short port;
} webserver;

struct pthreadClientHandleArgs {
  webserver *wserver;
  int socket;
  int alive;
};

typedef struct {
  int rc;
  char *prefix;
  char *reason;
} wsError;

struct httpRoute {
  const char *path;
  const char *method;
  struct httpResponse *httpResp;
};


char httpMethods[][MAX_HTTP_METHOD_LEN] = { "GET", "POST" };

struct httpResponse {
  int statusCode;
  int isFile;
  int contentSize;
  char *reasonPhrase;
  char *contentBuff;
};

struct httpRequest {
  float httpVersion;
  char *reqMethod;
  char *requestUri;
};

struct freeClientThreadArgs {
  struct httpRequest *httpReq;
  struct pthreadClientHandleArgs *clientHandleArgs;
  char *readBuff;
  char *respBuff;
  wsError *err;
};

// declares&inits error struct and assigns the prefix to the struct
// returns err struct ref
wsError* initWsError(char *prefix) {
  wsError *err = malloc(sizeof *err);
  err->prefix = malloc(sizeof(char) * MAX_ERR_PREFIX_LEN);
  err->reason = malloc(sizeof(char) * MAX_ERR_REASON_LEN);
  strncpy(err->prefix, prefix, MAX_ERR_PREFIX_LEN);  /* Flawfinder: ignore */ // ignored since src is limited by MAX_ERR_PREFIX_LEN, also the data is developer introduced
  return err;
}

// sets passed error struct reason attr to char arr
// supports format
// returns modified err struct reference
void setErr(wsError *err, const char* format, ...) {
  va_list argptr;
  va_start(argptr, format);
  vsnprintf(err->reason, MAX_ERR_REASON_LEN, format, argptr); /* Flawfinder: ignore */ // ignored since the data is developer introduced and len limited by vsnprintf
  va_end(argptr);

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
  fprintf(LOG_STREAM, format, argptr); /* Flawfinder: ignore */ // ignored since format is defined as const
  va_end(argptr);
  fflush(stdout);
}

// declares&inits route struct
// defines route struct attr
// returns reference to route struct
struct httpRoute *createRoute(const char *path, const char *method, struct httpResponse *resp) {
  struct httpRoute *route = malloc(sizeof *route);
  route->path = path;
  route->method = method;
  route->httpResp = resp;

  return route;
}

// frees all route structs attributes and struct itself from webserver struct
void freeRoutes(webserver *ws) {
  for (int i = 0; i < ws->nRoutes; i++) {
    if (ws->routes[i]->httpResp->isFile) {
      free(ws->routes[i]->httpResp->contentBuff);
    }
    free(ws->routes[i]->httpResp);
    free(ws->routes[i]);
  }
  free(ws->routes);
}

// adds route struct reference to webserver routes pointer arr
// dynamically re/allocates memory
void addRouteToWs(webserver *ws, struct httpRoute *route) {
  if (ws->nRoutes == 0) {
    ws->routes = malloc(sizeof *ws->routes);
    ws->routes[ws->nRoutes] = route;
  } else {
    ws->routes = (struct httpRoute**)realloc(ws->routes, (ws->nRoutes+1)*sizeof(struct httpRoute**));
    ws->routes[ws->nRoutes] = route;
  }
  ws->nRoutes++;
}

// removes space characters from given string ref
void removeSpaces(char* str, int strle) {
  int count = 0;
  for (int i = 0; i <= strle; i++)
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
char *readFileToBuffer(char *filename, int *size, wsError *err) {
  int stringSize, readSize;
  FILE *handler = fopen(filename, "r"); /* Flawfinder: ignore */ // check for \0 terminateion and bufferoverflow checks are implemented below,
  // since the system & files are developer handled and not meant to be modified during execution, race conditions are not considered
  char *buffer;

  if (handler) {
    fseek(handler, 0, SEEK_END);
    stringSize = ftell(handler);
    rewind(handler);

    buffer = malloc(sizeof(char) *stringSize+1);
    readSize = fread(buffer, sizeof(char), stringSize, handler);
    buffer[readSize] = 0;

    if (stringSize != readSize) {
      setErr(err, "file read sizes conflict \n");
      return NULL;
    }

    // always remember to close the file
    fclose(handler);
  } else {
    setErr(err, "file open error (file not found?) \n");
    return NULL;
  }
  err->rc = 0;
  *size = readSize;
  return buffer;
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

  /*
  ignoring respBuff flawfinder checks due to the data being developer introduced and thus cannot be exploited
  */

  // status line
  size += sprintf(respBuff+size, "HTTP/%s %d %s", HTTP_VERSION, resp->statusCode, resp->reasonPhrase); /* Flawfinder: ignore */
  size += sprintf(respBuff+size, "\r\n"); /* Flawfinder: ignore */
  // entity header
  size += sprintf(respBuff+size, "Content-type: text/html, text, plain"); /* Flawfinder: ignore */
  size += sprintf(respBuff+size, "\r\n"); /* Flawfinder: ignore */
  size += sprintf(respBuff+size, "Content-length: %d", resp->contentSize); /* Flawfinder: ignore */
  size += sprintf(respBuff+size, "\r\n"); /* Flawfinder: ignore */

  // content
  size += sprintf(respBuff+size, "\r\n"); /* Flawfinder: ignore */
  strncat(respBuff, resp->contentBuff, resp->contentSize); /* Flawfinder: ignore */

  // checking for buffer overflow
  // checking afterfwards and with assert due to developer caused overlow
  // checking beforehand would require extra memory and code which is spared
  assert(size < respBuffSize);

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
          req->reqMethod = malloc(sizeof(char)*iElementSize);
          memcpy(req->reqMethod, reqBuff+iElementUsedMem, iElementSize);  /* Flawfinder: ignore */ // in the line above memory is adequately allocated
          for (int i = 0; i<(sizeof(httpMethods)/MAX_HTTP_METHOD_LEN); i++) {
            if (strcmp(req->reqMethod, httpMethods[i]) == 0)
              break;
            if (i == (sizeof(httpMethods)/MAX_HTTP_METHOD_LEN))
              setErr(err, "http method not supported \n");
              return 0;
          }
          break;
        case 1:
          req->requestUri = malloc(sizeof(char)*iElementSize);
          memcpy(req->requestUri, reqBuff+iElementUsedMem, iElementSize);/* Flawfinder: ignore */ // in the line above memory is adequately allocated
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
void freeClientThread(void *args) {
  struct freeClientThreadArgs *argss = (struct freeClientThreadArgs*)args;
  free(argss->readBuff);
  free(argss->respBuff);
  free(argss->httpReq->requestUri);
  free(argss->httpReq->reqMethod);
  free(argss->err);

  pthread_mutex_lock(&argss->clientHandleArgs->wserver->mutexLock);
  argss->clientHandleArgs->alive = 0;
  pthread_mutex_unlock(&argss->clientHandleArgs->wserver->mutexLock);
}

// the clientHandle thread waits for incoming request and crafts the reply accordingly
// quits thread after reply/ does not continue to reply to multiple requests on one connection. does not support persistent connections
void *clientHandle(void *args) {
  struct pthreadClientHandleArgs *argss = (struct pthreadClientHandleArgs*)args;
  int socket = argss->socket;

  wsError *err = initWsError("error(clientHandle)->");

  char *readBuff = malloc(sizeof(char)*WS_BUFF_SIZE);
  char *respBuff = malloc(sizeof(char)*WS_BUFF_SIZE);
  struct httpRequest httpReq;
  struct httpResponse resp;

  int respSize, reqSize, sentBuffSize;
  int routeFound = 0;

  wsLog("new client thread created \n");

  int readBuffSize = read(socket, readBuff, WS_BUFF_SIZE); /* Flawfinder: ignore */ // buffer-overlow check follows in sec-checks
  struct freeClientThreadArgs freeArgs = {.httpReq = &httpReq, .clientHandleArgs = argss, .readBuff = readBuff, .respBuff = respBuff, .err = err};
  pthread_cleanup_push(freeClientThread, &freeArgs);

  if (readBuffSize == -1) {
    setErr(err, "tcp read error \n");
    printErr(err);

    close(socket);
    pthread_exit(NULL);
  }
  // sec checks
  if (readBuffSize >= WS_BUFF_SIZE) {
    setErr(err, "http req exceeds buffer size \n");
    printErr(err);

    close(socket);
    pthread_exit(NULL);
  }
  // \0 terminating readBuffer
  readBuff[readBuffSize] = (char)0;

  reqSize = parseHttpRequest(&httpReq, readBuff, readBuffSize, err);
  if (err->rc != 0){
    printErr(err);

    close(socket);
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

        close(socket);
        pthread_exit(NULL);
      }

      sentBuffSize = sendBuffer(socket, respBuff, strlen(respBuff), err); /* Flawfinder: ignore */ // \0 termination given by craftResp function
      if (err->rc != 0) {
        printErr(err);

        close(socket);
        pthread_exit(NULL);
      }
    }
  }
  pthread_mutex_unlock(&argss->wserver->mutexLock);

  if (!routeFound) {
    setErr(err, "page not found");
    resp.statusCode = 404;
    resp.reasonPhrase = "err";
    err->reason[MAX_ERR_REASON_LEN] = 0;
    resp.contentBuff = err->reason;
    resp.contentSize = strlen(resp.contentBuff); /* Flawfinder: ignore */ // \0 termination set in the line above
    respSize = craftResp(&resp, respBuff, WS_BUFF_SIZE, err);
    if (err->rc != 0){
      printErr(err);

      close(socket);
      pthread_exit(NULL);
    }

    sentBuffSize = sendBuffer(socket, respBuff, strlen(respBuff), err); /* Flawfinder: ignore */ // \0 termination given by craftResp function
    if (err->rc != 0) {
      printErr(err);

      close(socket);
      pthread_exit(NULL);
    }
  }

  #ifdef DEBUG
  printf("------------ response -------------\n");
  printf("%s \n", respBuff);
  printf("------------ response -------------\n");
  fflush(stdout);
  #endif

  wsLog("server-response sent \n");

  close(socket);
  pthread_cleanup_pop(1);
  pthread_exit(NULL);
}

// waits for new incoming connections on port x and creates clientHandles threads accordingly
void wsListen(webserver *wserver, wsError *err) {
  struct sockaddr_in tempClient;

  if (pthread_mutex_init(&wserver->mutexLock, NULL) != 0) {
    setErr(err, "mutex init failed \n");
    return;
  }

  wsLog("server listening \n");

  int newSocket;
  socklen_t addr_size;
  wserver->clientThreads = malloc(sizeof(struct clientThreadState));
  while(1)
  {
    addr_size = sizeof tempClient;
    newSocket = accept(wserver->wserverSocket, (struct sockaddr *) &tempClient, &addr_size);
    if (newSocket == -1) {
      setErr(err, "tcp accept error \n");
      return;
    }
    wsLog("new client connected \n");

    #ifdef DEBUG
    // telling the kernel that the socket is reused - only for debugging purposes
    int yes=1;
    if (setsockopt(newSocket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        setErr(err, "(debug flag set) socket reuse set failed \n");
        return;
    }
    #endif

    printf("clientIdThreadCounter: %d \n", wserver->clientIdThreadCounter);

    if (wserver->clientIdThreadCounter != 0) {
      wserver->clientThreads = realloc(wserver->clientThreads, sizeof(struct clientThreadState)*(wserver->clientIdThreadCounter+1));
      printf("%p \n", wserver->clientThreads);
    }
    // freed when thread is dead
    struct pthreadClientHandleArgs *clientArgs = malloc(sizeof clientArgs);
    clientArgs->wserver = wserver;
    clientArgs->socket = newSocket;
    clientArgs->alive = 1;
    wserver->clientThreads[wserver->clientIdThreadCounter].args = clientArgs;
    wserver->clientThreads[wserver->clientIdThreadCounter].clientThread = malloc(sizeof(pthread_t));

    if(pthread_create(wserver->clientThreads[wserver->clientIdThreadCounter].clientThread, NULL, clientHandle, (void*)wserver->clientThreads[wserver->clientIdThreadCounter].args) != 0 ) {
      setErr(err, "thread create error \n");
      return;
    } else {
      struct clientThreadState tempThreadState;
      for (int i = 0; i <= wserver->clientIdThreadCounter; i++) {
        pthread_mutex_lock(&wserver->mutexLock);
        if (!wserver->clientThreads[i].args->alive && i != 0) {
          // moving threadState ref to the end of the array in order to be able to free the mem
          tempThreadState = wserver->clientThreads[i];
          wserver->clientThreads[i] = wserver->clientThreads[wserver->clientIdThreadCounter];
          wserver->clientThreads[wserver->clientIdThreadCounter] = tempThreadState;

          // freeing threadState references
          // printf("ss \n");
          // free(wserver->clientThreads[wserver->clientIdThreadCounter].clientThread);
          // printf("cc \n");
          free(wserver->clientThreads[wserver->clientIdThreadCounter].args);
          wserver->clientIdThreadCounter--;
        }
        pthread_mutex_unlock(&wserver->mutexLock);
      }
      wserver->clientThreads = realloc(wserver->clientThreads, sizeof(struct clientThreadState)*(wserver->clientIdThreadCounter+1));
    }
  wserver->clientIdThreadCounter++;
  }
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
  webserver *wserver = malloc(sizeof *wserver);

  wsInit(wserver, 8080, err);
  if (err->rc != 0) {
    printErr(err);
    return 1;
  }
  wsLog("server initiated \n");

  struct httpResponse *mainRouteResponse = malloc(sizeof *mainRouteResponse);
  mainRouteResponse->statusCode = 200;
  mainRouteResponse->reasonPhrase = "succ";
  mainRouteResponse->contentBuff = "Hai";
  mainRouteResponse->contentSize = 3;
  struct httpRoute *mainRoute = createRoute("/", "GET", mainRouteResponse);
  addRouteToWs(wserver, mainRoute);


  struct httpResponse *routeResponse = malloc(sizeof *routeResponse);
  routeResponse->statusCode = 200;
  routeResponse->reasonPhrase = "succ";
  // by marking it as file, the dynamically allocated memory from the file buffer gets freed
  routeResponse->isFile = 1;
  routeResponse->contentBuff = readFileToBuffer("../testPage.html", &routeResponse->contentSize, err);
  if (err->rc != 0) {
    printErr(err);
    freeWs(wserver);
    free(err);
    return 1;
  }
  struct httpRoute *route = createRoute("/testPage", "GET", routeResponse);
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
