#ifndef __UTIL_H
#define __UTIL_H

#include <stdio.h>
#include <pthread.h>

void error_init();

void error(int consult_errno, int close_socket, pthread_mutex_t* unlock, const char* format, ...);

void* xrealloc(void* ptr, size_t size);

void* xmalloc(size_t size);

#ifndef DEBUG
#  define debug(msg, ...)
#else
#  include <sys/times.h>
#  define debug(msg, ...) fprintf(stderr, "%08x %4d: " msg "\n" , \
     (int) pthread_self(), (int) clock(), ##__VA_ARGS__)
#endif

#define CLIENT_ERROR(msg, ...) \
  error(0, client->socket, &client->serve->l_io, msg, ##__VA_ARGS__)
#define CLIENT_ERROR_ON_FAILURE(test, msg, ...) \
  if (test < 0) { error(1, client->socket, &client->serve->l_io, msg, ##__VA_ARGS__); }

#define SERVER_ERROR(msg, ...) \
  error(0, 0, NULL, msg, ##__VA_ARGS__)
#define SERVER_ERROR_ON_FAILURE(test, msg, ...) \
  if (test < 0) { error(1, 0, NULL, msg, ##__VA_ARGS__); }

#endif

