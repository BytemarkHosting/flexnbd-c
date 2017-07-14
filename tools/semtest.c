/*
 * Latency test for pthread 'conditions' vs using a good ol'
 * pair of linked sockets.
 * gcc -o semtest -O2 -g --std=c99 semtest.c -lpthread
 * 
 */
#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#define TESTCOUNT	10000000

pthread_mutex_t mutex;
pthread_cond_t kidwake, dadwake;
int flag_passed = 0;

static void *thread_semaphore(void * param)
{
	printf("kid started\n");
	pthread_mutex_lock(&mutex);
	pthread_cond_signal( &dadwake );
	pthread_mutex_unlock(&mutex);			
	do {
		pthread_mutex_lock(&mutex);
		while (flag_passed == 0)
			pthread_cond_wait( &kidwake, &mutex );
		flag_passed = 0;
		pthread_cond_signal( &dadwake );
		pthread_mutex_unlock(&mutex);			
	} while (1);
	return NULL;
}

int sock[2];

static void * thread_socket(void * param)
{
	printf("kid started\n");
	do {
		char b = 0;
		int r = read(sock[1], &b, 1);
		if (r != 1)
			perror("read");
		b = 0;
		write(sock[1], &b, 1);	// tell the kid
	} while (1);
	
	return NULL;
}


int main(int argc, char ** argv)
{
	int test = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "socket"))
			test = 1;
		else if (!strcmp(argv[i], "semaphore"))
			test = 2;
		else
			goto usage;
	}
	if (!test)
		goto usage;

	if (test == 1) {
		if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock)) {
			perror("socketpair");
			exit(1);
		}
		printf("Socket test start:\n");
		pthread_t t;
		pthread_create(&t, NULL, thread_socket, NULL);

		for (int ti = 0; ti < TESTCOUNT; ti++) {
			char b = 1;
			write(sock[0], &b, 1);	// tell the kid
			int r = read(sock[0], &b, 1);
			if (r != 1)
				perror("read");

			if (!(ti % 100000)) {
				printf("."); fflush(stdout);
			}
		}		
	} else {
		pthread_mutex_init(&mutex, NULL);
		pthread_cond_init( &kidwake, NULL );
		pthread_cond_init( &dadwake, NULL );

		printf("Semaphore test start:\n");
		pthread_t t;
		pthread_create(&t, NULL, thread_semaphore, NULL);

		pthread_mutex_lock(&mutex);
		printf("dad wait for kid\n");
		pthread_cond_wait( &dadwake, &mutex );
		pthread_mutex_unlock(&mutex);
		printf("dad has kid ready\n");
		
		for (int ti = 0; ti < TESTCOUNT; ti++) {
			pthread_mutex_lock(&mutex);
			flag_passed = 1;
			pthread_cond_signal( &kidwake );
			while (flag_passed == 1)
				pthread_cond_wait( &dadwake, &mutex );
			pthread_mutex_unlock(&mutex);
			if (!(ti % 100000)) {
				printf("."); fflush(stdout);
			}
		}
	}
	printf("\nDone\n");
	exit(0);
usage:
	fprintf(stderr, "%s socket -- run semaphore latency test with sockets\n", argv[0]);
	fprintf(stderr, "%s semaphore -- run semaphore latency test with sockets\n", argv[0]);
	exit(1);
}
