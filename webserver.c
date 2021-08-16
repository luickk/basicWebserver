#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#define MAX_CLIENTS 10

typedef struct {
  unsigned short port;
  char buf[1024];
  struct sockaddr_in server;
  int wserverSocket;
  pthread_t clientThreads[MAX_CLIENTS];
  int clientIdThreadCounter;
  pthread_mutex_t mutexLock;
} webserver;

int wsInit(webserver *wserver, int port) {
  wserver->port = port;

  if ((wserver->wserverSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("socket err");
    return 1;
  }

  wserver->server.sin_family = AF_INET;
  wserver->server.sin_port   = htons(port);
  wserver->server.sin_addr.s_addr = INADDR_ANY;

  wserver->mutexLock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
  wserver->clientIdThreadCounter = 0;

  if (bind(wserver->wserverSocket, (struct sockaddr *)&wserver->server, sizeof(wserver->server)) < 0)
  {
    printf("bind err");
    return 1;
  }

  if (listen(wserver->wserverSocket, 1) != 0)
  {
    printf("listenerr");
    return 1;
  }
  return 0;
}

void *clientHandle(void *arg) {
  printf("new client thread init sleep 1 \n");
  sleep(1);
  printf("sleep 1 passed exit \n");
  pthread_exit(NULL);
}

int wsListen(webserver *wserver) {
  struct sockaddr_in tempClient;
  int newSocket;
  socklen_t addr_size;
  while(1)
  {
    addr_size = sizeof tempClient;
    newSocket = accept(wserver->wserverSocket, (struct sockaddr *) &tempClient, &addr_size);

    if(pthread_create(&wserver->clientThreads[wserver->clientIdThreadCounter], NULL, clientHandle, &clientHandle) != 0 ) {
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


  printf("Server ended successfully\n");
  return 0;
}
