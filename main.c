// todo: daemonize
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <getopt.h>

#define MAX_BACKLOG 10
#define LINE_BUF_SIZE 256
#define WORKER_SIZE 4
#define BUFFER_MAX_SIZE 10
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
  fprintf(out, "HTTP/1.1 404 Not Found\r\n");
  fprintf(out, "\r\n");
  fflush(out);
}

void respond(FILE* out, HTTPRequest* request) {
  if (strcasecmp(request->method, "GET") != 0) {
    render_ok(out, "hello");
    return;
  }

  if (strcasecmp(request->path, "/sleep") == 0) {
    sleep(1);
    render_ok(out, "ok");
    return;
  }

  char path[LINE_BUF_SIZE];
  sprintf(path, "%s%s", SERVE_DIR, request->path);
  struct stat s;
  if (lstat(path, &s) < 0) {
    perror("lstat");
    render_not_found(out);
    return;
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
  fclose(f);
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

int listen_socket(const char* port) {
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
    int sock = socket(current_addrinfo->ai_family, current_addrinfo->ai_socktype, current_addrinfo->ai_protocol);

    // ref: https://stackoverflow.com/questions/5106674/error-address-already-in-use-while-binding-socket-with-address-but-the-port-num
    int option = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    if (sock == -1) {
      continue;
    }

    if (bind(sock, current_addrinfo->ai_addr, current_addrinfo->ai_addrlen) < 0) {
      close(sock);
      continue;
    }

    if (listen(sock, MAX_BACKLOG) < 0) {
      close(sock);
      continue;
    }

    freeaddrinfo(res);
    return sock;
  }

  return -1;
}


typedef struct RequestBuffer {
  int buffer[BUFFER_MAX_SIZE];
  int head;
  int tail;
  int count;

  pthread_mutex_t mutex;
  pthread_cond_t get_wait;
  pthread_cond_t put_wait;
} RequestBuffer;

void request_buffer_init(RequestBuffer* buffer) {
  buffer->head = 0;
  buffer->tail = 0;
  buffer->count = 0;
  pthread_mutex_init(&(buffer->mutex), NULL);
  pthread_cond_init(&(buffer->get_wait), NULL);
  pthread_cond_init(&(buffer->put_wait), NULL);
}

void request_buffer_put(RequestBuffer* buffer, int value) {
  buffer->buffer[buffer->tail] = value;
  buffer->count++;
  buffer->tail = (buffer->tail + 1) % BUFFER_MAX_SIZE;
}

int request_buffer_get(RequestBuffer* buffer) {
  int value = buffer->buffer[buffer->head];
  buffer->count--;
  buffer->head = (buffer->head + 1) % BUFFER_MAX_SIZE;
  return value;
}

void request_buffer_wait_and_put(RequestBuffer* buffer, int value) {
  pthread_mutex_lock(&buffer->mutex);
  while (buffer->count == BUFFER_MAX_SIZE) {
    pthread_cond_wait(&buffer->put_wait, &buffer->mutex);
  }

  request_buffer_put(buffer, value);
  pthread_cond_signal(&buffer->get_wait);
  pthread_mutex_unlock(&buffer->mutex);
}

int request_buffer_wait_and_get(RequestBuffer* buffer) {
  pthread_mutex_lock(&buffer->mutex);
  while (buffer->count == 0) {
    pthread_cond_wait(&buffer->get_wait, &buffer->mutex);
  }

  int request = request_buffer_get(buffer);
  pthread_cond_signal(&buffer->put_wait);
  pthread_mutex_unlock(&buffer->mutex);

  return request;
}


RequestBuffer raw_request_buffer;

void* worker(void* arg) {
  int num = *((int*)arg);
  RequestBuffer* request_buffer = &raw_request_buffer;

  printf("worker %d: ready\n", num);

  while (1) {
    int fd = request_buffer_wait_and_get(request_buffer);

    FILE* in = fdopen(fd, "r");
    HTTPRequest* request = read_request(in);
    if (request == NULL) {
      log_exit("invalid request");
    }
    printf("worker %d:\n", num);
    print_request(request);
    printf("\n");

    FILE* out = fdopen(fd, "w");
    respond(out, request);

    fclose(in);
    fclose(out);
    close(fd);
  }
}

static struct option longopts[] = {
  {"port", optional_argument, NULL, 'p'},
  {"help", no_argument, NULL, 'h'},
  {0, 0, 0, 0}
};

int main(int argc, char* argv[]) {
  int opt;
  char* port = "8008";

  while ((opt = getopt_long(argc, argv, "p:h", longopts, NULL)) != -1) {
    switch (opt) {
      case 'p':
        port = optarg;
        break;

      case 'h':
        fprintf(stderr, "Usage %s [-p PORT]\n", argv[0]);
        exit(0);
    }
  }

  printf("Listening on port %s\n", port);
  int sock = listen_socket(port);
  if (sock < 0) {
    log_exit("listen failed");
  }

  RequestBuffer* request_buffer = &raw_request_buffer;
  request_buffer_init(request_buffer);

  pthread_t* threads = malloc(WORKER_SIZE * sizeof(pthread_t));
  for (int i = 0; i < WORKER_SIZE; i++) {
    int* arg = malloc(sizeof(int));
    arg[0] = i;
    int rc = pthread_create(&threads[i], NULL, worker, arg);
    if (rc < 0) {
      perror(NULL);
      log_exit("thread create");
    }
  }

  for (;;) {
    struct sockaddr addr;
    socklen_t addrlen = sizeof(addr);

    int fd = accept(sock, &addr, &addrlen);
    if (fd < 0) {
      // todo: accept: Invalid argument
      log_exit("accept");
    }

    request_buffer_wait_and_put(request_buffer, fd);
  }

  close(sock);
}
