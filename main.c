#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_BACKLOG 5
#define LINE_BUF_SIZE 100
#define SERVE_DIR "./dist"

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

void render_ok_response_header(FILE* out, const char* content_type) {
  fprintf(out, "HTTP/1.1 200 OK\r\n");
  fprintf(out, "Content-Type: %s\r\n", content_type);
}

void render_ok(FILE* out, const char* str) {
  render_ok_response_header(out, "text/plain");

  fprintf(out, "Content-Length: %d\r\n", (int)strlen(str));
  fprintf(out, "\r\n");
  fprintf(out, "%s", str);
  fflush(out);
}

void render_not_found(FILE* out) {
  fprintf(out, "HTTP/1.1 404 NotFound\r\n");
}

void respond(FILE* out, HTTPRequest* request) {
  if (strcasecmp(request->method, "GET") != 0) {
    render_ok(out, "hello");
  }

  char* path = malloc(LINE_BUF_SIZE * sizeof(char));
  sprintf(path, "%s%s", SERVE_DIR, request->path);
  struct stat s;
  if (lstat(path, &s) < 0) {
    perror("lstat");
    log_exit("lstat failed");
  }

  int size = s.st_size;

  FILE* f = fopen(path, "r");
  if (f == NULL) {
    render_not_found(out);
    return;
  }

  render_ok_response_header(out, "text/html");
  fprintf(out, "Content-Length: %d\r\n", size);
  fputs("\r\n", out);

  char buf[LINE_BUF_SIZE];
  while (fgets(buf, LINE_BUF_SIZE, f) != NULL) {
    fputs(buf, out);
  }

  if (!feof(f)) {
    log_exit("read file failed");
  }

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

HTTPRequest* read_request(FILE* in) {
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
      return NULL;
    }
  } else {
    request->body = NULL;
  }

  return request;
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
    HTTPRequest* request = read_request(in);
    if (request == NULL) {
      log_exit("invalid request");
    }
    print_request(request);

    FILE* out = fdopen(fd, "w");
    respond(out, request);

    fclose(in);
    fclose(out);
  }

  close(sock);
}
