/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __SO_CONSUMER_H__
#define __SO_CONSUMER_H__

#include "ring_buffer.h"
#include "packet.h"

typedef struct so_consumer_ctx_t {
	struct so_ring_buffer_t *producer_rb;

	int out_fd;

	// variabila folosita pentru a calcula id fiecarui thread
	int num_thread;

	int total_consumer_threads;

    /* TODO: add synchronization primitives for timestamp ordering */
	pthread_mutex_t mutex_output;
	pthread_mutex_t mutex_timestamps;
	pthread_cond_t semnal_scrie_log;
	pthread_cond_t semnal_citeste_log;
} so_consumer_ctx_t;



typedef struct so_buffer_entry_t {
	unsigned long timestamp;
	char *data;
} so_buffer_entry_t;

typedef struct so_output_buffer_t {
	struct so_buffer_entry_t *buffer;

	size_t size_buffer;

	size_t num_consumers;

	int num_threaduri_scris;

	bool *este_terminat;

	// fiecare thread are anumite pozitii prestabilite in care poate scrie
	int **pozitii_libere;

	int *num_pozitii_libere;

	pthread_mutex_t mutex_thread_a_terminat;

	pthread_cond_t **semnal_buffer;
	pthread_mutex_t **blocare_buffer;
} so_output_buffer_t;

int create_consumers(pthread_t *tids,
					int num_consumers,
					so_ring_buffer_t *rb,
					const char *out_filename);

#endif /* __SO_CONSUMER_H__ */
