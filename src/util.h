#ifndef __UTIL_H
#define __UTIL_H

#include <stdio.h>
#include <pthread.h>

void error_init();

void error(int consult_errno, int close_socket, pthread_mutex_t* unlock, const char* format, ...);

void* xrealloc(void* ptr, size_t size);

void* xmalloc(size_t size);

void set_debug(int value);
#ifdef DEBUG
void debug(const char*msg, ...);
#else
/* no-op */
#  define debug( msg, ...)
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

