#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BACKLOG 5

void log_exit(char* name) {
  perror(name);
  exit(EXIT_FAILURE);
}

int main(int argc, char* argv[]) {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == -1) {
    fprintf(stderr, "socket() failed\n");
  }

  char* port = "8008";
  struct addrinfo hints, *res;

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  int err = getaddrinfo(NULL, port, &hints, &res);
  if (err != 0) {
    log_exit("getaddrinfo");
  }

  struct addrinfo *current_addrinfo;
  for (current_addrinfo = res; current_addrinfo != NULL; current_addrinfo = current_addrinfo->ai_next) {
    if (bind(sock, current_addrinfo->ai_addr, current_addrinfo->ai_addrlen) < 0) {
      continue;
    }

    if (listen(sock, MAX_BACKLOG) < 0) {
      continue;
    }
  }

  freeaddrinfo(res);
  for (;;) {
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);

    int fd = accept(sock, &addr, &addrlen);
    if (fd < 0) {
      log_exit("accept");
    }
    printf("accept\n");
    sleep(1);
    close(fd);
  }

  close(sock);
}
