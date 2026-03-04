// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

const int THREAD_BUFFER_SIZE = 10;

struct so_consumer_ctx_t argument;
struct so_output_buffer_t output;

/*
 *	fiecare thread are 5 casute in care poate sa scrie
 *	thread ul suplimentar face o decizie de scriere doar cand fiecare thread a scris macar un pachet
 *	in buffer
 */

void output_buffer_init(int num_consumers)
{
	output.size_buffer = num_consumers * THREAD_BUFFER_SIZE;
	output.buffer = malloc(output.size_buffer * sizeof(so_buffer_entry_t));

	output.blocare_buffer = malloc(num_consumers * sizeof(pthread_mutex_t *));
	output.semnal_buffer = malloc(num_consumers * sizeof(pthread_cond_t *));
	output.pozitii_libere = malloc(num_consumers * sizeof(int *));
	output.este_terminat = malloc(num_consumers * sizeof(bool));
	output.num_pozitii_libere = malloc(num_consumers * sizeof(int));

	pthread_mutex_init(&output.mutex_thread_a_terminat, NULL);

	output.num_consumers = num_consumers;

	output.num_threaduri_scris = 0;

	int aux = 0;

	// se aloca memorie pentru fiecare camp folosit de threaduri
	for (int i = 0; i < num_consumers; i++) {
		output.blocare_buffer[i] = malloc(sizeof(pthread_mutex_t));
		output.semnal_buffer[i] = malloc(sizeof(pthread_cond_t));
		output.pozitii_libere[i] = malloc(sizeof(int) * THREAD_BUFFER_SIZE);

		output.este_terminat[i] = false;
		output.num_pozitii_libere[i] = THREAD_BUFFER_SIZE;

		pthread_mutex_init(output.blocare_buffer[i], NULL);
		pthread_cond_init(output.semnal_buffer[i], NULL);

		for (int j = 0; j < THREAD_BUFFER_SIZE; j++)
			output.pozitii_libere[i][j] = aux++;
	}

	for (size_t i = 0; i < output.size_buffer; i++)
		output.buffer[i].data = malloc(sizeof(char) * PKT_SZ);
}


void destroy_buffer(void)
{
	for (size_t i = 0; i < output.size_buffer; i++)
		free(output.buffer[i].data);

	for (size_t i = 0; i < output.num_consumers; i++) {
		pthread_mutex_destroy(output.blocare_buffer[i]);
		pthread_cond_destroy(output.semnal_buffer[i]);

		free(output.semnal_buffer[i]);
		free(output.blocare_buffer[i]);
		free(output.pozitii_libere[i]);
	}

	free(output.num_pozitii_libere);
	free(output.este_terminat);
	free(output.pozitii_libere);
	free(output.semnal_buffer);
	free(output.blocare_buffer);
	free(output.buffer);
}

void verifica_minim(int value, int *minim, int *consumer_index, int *buffer_index, int i, int j)
{
	if (value < *minim) {
		*minim = value;
		// se salveaza pozitia la care s a gasit pachetul
		*consumer_index = i;
		*buffer_index = j;
	}
}
// functie folosita de trheadul auxiliar folosit pt scrierea in fisierul log
void *aux_thread(void *aux)
{
	so_consumer_ctx_t *ctx = (so_consumer_ctx_t *) aux;

	char buffer[PKT_SZ];

	while (1) {
		/* PAS 1 : verifica daca este vreun pachet in buffer */

		int minim = 10000000;
		int consumer_index = -1, buffer_index = -1;
		bool valid;


		for (int i = 0; i < ctx->total_consumer_threads; i++) {
			// se ignora threadurile care s au inchis
			pthread_mutex_lock(&output.mutex_thread_a_terminat);

			// se verifica daca threadul i s a inchis si are bufferul gol
			if (output.este_terminat[i] == true && output.num_pozitii_libere[i] == THREAD_BUFFER_SIZE) {
				pthread_mutex_unlock(&output.mutex_thread_a_terminat);

				// se trece la urmatorul thread
				continue;
			}

			pthread_mutex_unlock(&output.mutex_thread_a_terminat);

			pthread_mutex_lock(output.blocare_buffer[i]);

			while (1) {
				// se verifica faptul ca fiecare thread a scris macar un pachet in bufferul auxiliar
				valid = false;
				for (int j = 0; j < THREAD_BUFFER_SIZE; j++) {
					if (output.pozitii_libere[i][j] == -1) {
						// se semnaleaza ca exista un pachet in buffer
						valid = true;

						int value = output.buffer[i * THREAD_BUFFER_SIZE + j].timestamp;

						// se calculeaza valoarea minima dupa timestamp
						verifica_minim(value, &minim, &consumer_index, &buffer_index, i, j);
					}
				}

				// se verifica daca exista un pachet in buffer sau daca threadul a terminat
				if (valid || output.este_terminat[i] == true) {
					pthread_mutex_unlock(output.blocare_buffer[i]);
					break;
				}

				// semnaleaza consumatorul ca trebuie sa scrie un pachet
				pthread_cond_signal(output.semnal_buffer[i]);

				// threadul asteapta consumatorul i sa scrie
				pthread_cond_wait(output.semnal_buffer[i], output.blocare_buffer[i]);
			}
		}

		/* PAS 2: se extrage pachetul cu timestampul minim din bufferul auxiliar */

		// daca nu mai exista niciun pachet in buffer se iese
		if (consumer_index == -1 || buffer_index == -1)
			break;

		pthread_mutex_lock(output.blocare_buffer[consumer_index]);

		// se scrie pachetul din bufferul auziliar in bufferul threadului
		memcpy(buffer, output.buffer[consumer_index * THREAD_BUFFER_SIZE + buffer_index].data, PKT_SZ);

		// se macheaza pozitia ca fiind libera
		output.pozitii_libere[consumer_index][buffer_index] = consumer_index * THREAD_BUFFER_SIZE + buffer_index;

		// se incrementeaza numarul de pozitii libere
		pthread_mutex_lock(&output.mutex_thread_a_terminat);
		output.num_pozitii_libere[consumer_index]++;
		pthread_mutex_unlock(&output.mutex_thread_a_terminat);

		pthread_mutex_unlock(output.blocare_buffer[consumer_index]);

		// se scrie in fisierul de output
		write(ctx->out_fd, buffer, strlen(buffer));
	}

	return NULL;
}

// functie folosita de threadurile consumator
void *consumer_thread(void *aux)
{
	so_consumer_ctx_t *ctx = (so_consumer_ctx_t *) aux;

	struct so_ring_buffer_t *ring = ctx->producer_rb;

	char packet[PKT_SZ], log_entry[PKT_SZ];
	int r, action, thread_id;
	unsigned long hash = 0, timestamp = 0;

	// se asociaza un id threadului curent
	pthread_mutex_lock(&ctx->mutex_timestamps);
	thread_id = ctx->num_thread++;
	pthread_mutex_unlock(&ctx->mutex_timestamps);

	while (1) {
		/* PAS 1 : citire din buffer */

		// se preia un pachet din buffer
		r = ring_buffer_dequeue(ring, packet, PKT_SZ);

		// se inchide thread ul daca buffer-ul s a inchis si este gol
		if (r == -1)
			break;

		/* PAS 2 : procesare pachet */
		action = process_packet((struct so_packet_t *)packet);

		hash = packet_hash((struct so_packet_t *)packet);
		timestamp = ((struct so_packet_t *)packet)->hdr.timestamp;

		snprintf(log_entry, PKT_SZ, "%s %016lx %lu\n",
				RES_TO_STR(action), hash, timestamp);


		/* PAS 3 : scrierea in bufferul de output */

		// verifica daca are loc sa scrie
		pthread_mutex_lock(output.blocare_buffer[thread_id]);

		int index;

		while (1) {
			index = -1;

			// verifica daca este loc in bufferul auxiliar
			for (int i = 0; i < THREAD_BUFFER_SIZE; i++) {
				if (output.pozitii_libere[thread_id][i] != -1) {
					// se salveaza indexul la care trebuie sa scrie
					index = output.pozitii_libere[thread_id][i];

					// se marcheaza pozitia ca fiind ocupata
					output.pozitii_libere[thread_id][i] = -1;

					pthread_mutex_lock(&output.mutex_thread_a_terminat);
					output.num_pozitii_libere[thread_id]--;
					pthread_mutex_unlock(&output.mutex_thread_a_terminat);

					// se copiaza datele in bufferul auxiliar
					output.buffer[index].timestamp = timestamp;
					memcpy(output.buffer[index].data, log_entry, PKT_SZ);
					break;
				}
			}

			if (index != -1)
				break;

			// asteapta pana se elibereaza loc
			pthread_cond_wait(output.semnal_buffer[thread_id], output.blocare_buffer[thread_id]);
		}

		// se semnaleaza threadul auxiliar sa verifice daca noul pachet indeplineste conditia
		pthread_cond_signal(output.semnal_buffer[thread_id]);

		pthread_mutex_unlock(output.blocare_buffer[thread_id]);
	}

	pthread_mutex_lock(&output.mutex_thread_a_terminat);

	// se seteaza statusul threadului curent ca fiind terminat
	output.este_terminat[thread_id] = true;

	pthread_mutex_unlock(&output.mutex_thread_a_terminat);

	pthread_mutex_lock(output.blocare_buffer[thread_id]);
	pthread_cond_signal(output.semnal_buffer[thread_id]);
	pthread_mutex_unlock(output.blocare_buffer[thread_id]);

	return NULL;
}

int create_consumers(pthread_t *tids,
					 int num_consumers,
					 struct so_ring_buffer_t *rb,
					 const char *out_filename)
{
	// se deschide fisierul log
	int r = open(out_filename, O_RDWR|O_CREAT|O_TRUNC, 0666);

	DIE(r < 0, "open");

	// se initializeaza mutexurile
	pthread_mutex_init(&argument.mutex_timestamps, NULL);
	pthread_mutex_init(&argument.mutex_output, NULL);
	pthread_cond_init(&argument.semnal_scrie_log, NULL);
	pthread_cond_init(&argument.semnal_citeste_log, NULL);

	argument.out_fd = r;
	argument.num_thread = 0;
	argument.total_consumer_threads = num_consumers;
	argument.producer_rb = rb;

	// se initializeaza bufferul auxiliar
	output_buffer_init(num_consumers);

	// se creaza threadurile consumer
	for (int i = 0; i < num_consumers; i++)
		pthread_create(&tids[i], NULL, consumer_thread, (void *)&argument);

	// se creaza un thread suplimentar
	pthread_create(&tids[num_consumers], NULL, aux_thread, (void *)&argument);

	return num_consumers;
}
