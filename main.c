#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BACKLOG 5
#define LINE_BUF_SIZE 100

typedef struct HTTPRequestHeader {
  char* key;
  char* value;
  struct HTTPRequestHeader* next;
} HTTPRequestHeader;
typedef struct HTTPRequest {
  char* method;
  char* path;
  struct HTTPRequestHeader* header;
  int length;
  char* body;
} HTTPRequest;

void log_exit(char* name) {
  perror(name);
  exit(EXIT_FAILURE);
}

void respond(FILE* out) {
  fprintf(out, "HTTP/1.1 200 OK\r\n");
  fprintf(out, "Content-Type: text/plain\r\n");

  char* str = "hello";
  fprintf(out, "Content-Length: %d\r\n", (int)strlen(str));
  fprintf(out, "\r\n");
  fprintf(out, "%s", str);
  fflush(out);
}

HTTPRequestHeader* read_request_header(FILE* in) {
  HTTPRequestHeader* header = malloc(sizeof(struct HTTPRequestHeader));
  header->key = malloc(LINE_BUF_SIZE * sizeof(char));
  header->value = malloc(LINE_BUF_SIZE * sizeof(char));
  header->next = NULL;

  char line[LINE_BUF_SIZE];
  if (fgets(line, LINE_BUF_SIZE, in) == NULL) {
    log_exit("header fgets");
  }

  if (line[0] == '\n' || strcmp(line, "\r\n") == 0)  {
    return NULL;
  }

  char* buf = strdup(line);
  char* key = strsep(&buf, ":");
  buf++;
  char* value = strsep(&buf, "\r\n");

  strcpy(header->key, key);
  strcpy(header->value, value);

  return header;
}

void print_request(struct HTTPRequest* request) {
  printf("method:%s\tpath:%s\n", request->method, request->path);

  for (struct HTTPRequestHeader* header = request->header; header != NULL; header = header->next) {
    printf("%s:%s\n", header->key, header->value);
  }

  if (request->length > 0) {
    printf("\n%s\n", request->body);
  }
}

HTTPRequestHeader* find_header(HTTPRequest* request, const char* key) {
  for (HTTPRequestHeader* header = request->header; header != NULL; header = header->next) {
    if (strcasecmp(header->key, key) == 0) {
      return header;
    }
  }

  return NULL;
}

void read_request(FILE* in) {
  struct HTTPRequest* request = malloc(sizeof(struct HTTPRequest));
  request->method = malloc(LINE_BUF_SIZE * sizeof(char));
  request->path = malloc(LINE_BUF_SIZE * sizeof(char));

  char line[LINE_BUF_SIZE];
  if (fgets(line, LINE_BUF_SIZE, in) == NULL) {
    log_exit("no request line");
  }

  char* buf = strdup(line);
  char* token;
  token = strsep(&buf, " ");
  strcpy(request->method, token);

  token = strsep(&buf, " ");
  strcpy(request->path, token);

  struct HTTPRequestHeader* dummy = malloc(sizeof(struct HTTPRequestHeader));
  struct HTTPRequestHeader* current_header = dummy;
  while (1) {
    struct HTTPRequestHeader* header = read_request_header(in);
    if (header == NULL) break;

    if (current_header != NULL) current_header->next = header;
    current_header = header;
  }
  request->header = dummy->next;
  free(dummy);

  HTTPRequestHeader* content_length_header = find_header(request, "Content-Length");
  if (content_length_header != NULL) {
    request->length = atoi(content_length_header->value);
  } else {
    request->length = 0;
  }

  if (request->length > 0) {
    request->body = malloc(request->length * sizeof(char));
    if (fread(request->body, request->length, 1, in) < 0) {
      fprintf(stderr, "request body is too short");
      return;
    }
  } else {
    request->body = NULL;
  }

  print_request(request);
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
      // todo: accept: Invalid argument
      log_exit("accept");
    }

    FILE* in = fdopen(fd, "r");
    read_request(in);

    FILE* out = fdopen(fd, "w");
    respond(out);

    fclose(in);
    fclose(out);
  }

  close(sock);
}
