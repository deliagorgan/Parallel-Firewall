// SPDX-License-Identifier: BSD-3-Clause

#include "ring_buffer.h"

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
	ring->data = (char *) malloc(sizeof(char) * cap);
	DIE(ring->data == NULL, "malloc");

	ring->len = 0;
	ring->cap = cap;
	ring->write_pos = 0;
	ring->read_pos = 0;
	ring->buffer_oprit = 0;

	pthread_mutex_init(&ring->mutex_len, NULL);
	pthread_cond_init(&ring->semnal_poate_scrie, NULL);
	pthread_cond_init(&ring->semnal_poate_citi, NULL);

	return 1;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
	pthread_mutex_lock(&ring->mutex_len);

	// se asteapta pana este loc de scris
	while (ring->len + size > ring->cap)
		pthread_cond_wait(&ring->semnal_poate_scrie, &ring->mutex_len);

	char *ptr = (char *) data;

	// se scrie in ring buffer
	for (size_t i = 0; i < size; i++) {
		ring->data[ring->write_pos] = ptr[i];

		ring->write_pos = (ring->write_pos + 1) % ring->cap;
	}
	ring->len += size;

	// se semnaleaza consumatorii ca pot citi din buffer
	pthread_cond_broadcast(&ring->semnal_poate_citi);

	pthread_mutex_unlock(&ring->mutex_len);

	return size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size)
{
	pthread_mutex_lock(&ring->mutex_len);

	// se verifica daca nu exista nimic de citit
	while (ring->len == 0) {
		// se verifica daca bufferul a fost inchis de catre producator
		if (ring->buffer_oprit == -1) {
			pthread_mutex_unlock(&ring->mutex_len);
			return -1;
		}

		// se asteapta pana este ceva de citit
		pthread_cond_wait(&ring->semnal_poate_citi, &ring->mutex_len);
	}

	ring->len -= size;

	size_t read_pos = ring->read_pos;

	ring->read_pos = (ring->read_pos + size) % ring->cap;

	char *ptr = (char *) data;

	// se citeste din ring buffer
	for (size_t i = 0; i < size; i++) {
		ptr[i] = ring->data[read_pos];

		read_pos = (read_pos + 1) % ring->cap;
	}

	// se semnaleaza producatorul ca poate scrie in buffer
	pthread_cond_signal(&ring->semnal_poate_scrie);

	pthread_mutex_unlock(&ring->mutex_len);

	return size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	free(ring->data);
	pthread_mutex_destroy(&ring->mutex_len);
	pthread_cond_destroy(&ring->semnal_poate_citi);
	pthread_cond_destroy(&ring->semnal_poate_scrie);
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	pthread_mutex_lock(&ring->mutex_len);
	ring->buffer_oprit = -1;
	pthread_mutex_unlock(&ring->mutex_len);
}
