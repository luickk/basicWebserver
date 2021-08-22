#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_CLIENTS 10

#define CRLF 0x0D0A
#define SP 0x0A

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

struct httpRequest {
  int reqMethod;
  double httpVersion;
  char requestUri[];

  // char *browserAccept;
  // char *browserUserAgent;
  // char *browserAccEncoding;
  // char *accLang;
  // char *connType;
};

void parseHttpRequest(char requestBuffer[], struct httpRequest *req) {
  char *tok;
  const char crlf = (char)CRLF;
  char *buffCpy = strdup(requestBuffer);

  while ((tok = strsep(&buffCpy, &crlf)) != NULL) {
    printf("Line: %s \n", tok);
  }
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
  char buff[1024] = {};

  read(argss->socket, buff, sizeof(buff));

  printf("From client: %s \n ", buff);
  printf("----------------------------- \n");
  parseHttpRequest(buff, httpReq);

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
