
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "isal_multithread_perf.h"

#ifndef HASH_THREAD
/* MD5 related params and structures*/
#define DIGEST_NWORDS   MD5_DIGEST_NWORDS
#define MB_BUFS         MD5_MAX_LANES
#define HASH_CTX_MGR    MD5_HASH_CTX_MGR
#define HASH_CTX	MD5_HASH_CTX

#define OSSL_THREAD_FUNC	md5_ossl_func
#define OSSL_HASH_FUNC		MD5
#define MB_THREAD_FUNC		md5_mb_func
#define CTX_MGR_INIT		md5_ctx_mgr_init
#define CTX_MGR_SUBMIT		md5_ctx_mgr_submit
#define CTX_MGR_FLUSH		md5_ctx_mgr_flush

#define rounds_buf MD5_MAX_LANES

#endif // HASH_THREAD

typedef uint32_t hash_digests[DIGEST_NWORDS];

void *OSSL_THREAD_FUNC(void *arg)
{
	int32_t id = *((int *)arg);
	uint32_t i = 0, j = 0;
	char *hash_buf[rounds_buf] = { NULL };	/* hash buf is used to do hash compute */
	char *carry_buf[rounds_buf] = { NULL };	/* carry buf is used to do memory movement */
	hash_digests digest;
	uint64_t round = -1;
	struct timeval start_tv, stop_tv;
	long long secs = run_secs;

	printfv("Thread %i is started\n", id);
	/* memory allocate */
	for (j = 0; j < rounds_buf; j++) {
		carry_buf[j] = (char *)calloc((size_t)buflen, 1);
		if (carry_buf[j] == NULL) {
			printf("calloc failed test aborted\n");
			goto out;
		}

		hash_buf[j] = (char *)calloc((size_t)buflen, 1);
		if (hash_buf[j] == NULL) {
			printf("calloc failed test aborted\n");
			goto out;
		}

		/* Create the random data */
		for (i = 0; i < buflen; i += 1024) {
			carry_buf[j][i] = i % 256;
			hash_buf[j][i] = i % 256;
		}
	}

	/* Thread sync */
	pthread_mutex_lock(&count_lock);
	count++;
	if (count == num_threads) {
		pthread_cond_broadcast(&count_cond);
	} else {
		pthread_cond_wait(&count_cond, &count_lock);
	}
	pthread_mutex_unlock(&count_lock);

	printfv("Thread %i is ready\n", id);
	/* hash func starts to run */
	round = 0;
	gettimeofday(&start_tv, 0);
	gettimeofday(&stop_tv, 0);
	while (secs > (stop_tv.tv_sec - start_tv.tv_sec)) {
		for (j = 0; j < rounds_buf; j++) {
			/* Pre mem-operation */
			if (prememcpy)
				memcpy(hash_buf[j], carry_buf[j], buflen);

			/* Calculate hash digest */
			OSSL_HASH_FUNC((char *)hash_buf[j], buflen, (unsigned char *)&digest);

			/* Post mem-operation */
			if (postmemcpy)
				memcpy(carry_buf[j], hash_buf[j], buflen);
		}
		round++;

		gettimeofday(&stop_tv, 0);
	}
	printfv("thread %2i, openssl_func rounds %ld\n", id, round);

      out:
	for (j = 0; j < rounds_buf; j++) {
		free(carry_buf[j]);
		free(hash_buf[j]);
	}

	pthread_exit((void *)round);
}

void *MB_THREAD_FUNC(void *arg)
{
	int32_t id = *((int *)arg);
	uint32_t i = 0, j = 0;
	char *hash_buf[rounds_buf] = { NULL };	/* hash buf is used to do hash compute */
	char *carry_buf[rounds_buf] = { NULL };	/* carry buf is used to do memory movement */
	hash_digests *digests[rounds_buf];
	uint64_t round = -1;
	struct timeval start_tv, stop_tv;
	long long secs = run_secs;

	HASH_CTX_MGR *mgr = NULL;
	HASH_CTX *ctxpool = NULL, *ctx = NULL;

	printfv("Thread %i is started\n", id);
	/* Memory allocate */
	for (j = 0; j < rounds_buf; j++) {
		carry_buf[j] = (char *)calloc((size_t)buflen, 1);
		if (carry_buf[j] == NULL) {
			printf("calloc failed test aborted\n");
			goto out;
		}

		hash_buf[j] = (char *)calloc((size_t)buflen, 1);
		if (hash_buf[j] == NULL) {
			printf("calloc failed test aborted\n");
			goto out;
		}

		digests[j] = (hash_digests *) calloc(sizeof(hash_digests), 1);

		/* Create the random data */
		for (i = 0; i < buflen; i += 1024) {
			carry_buf[j][i] = i % 256;
			hash_buf[j][i] = i % 256;
		}
	}

	ctxpool = (HASH_CTX *) calloc(rounds_buf, sizeof(HASH_CTX));
	for (i = 0; i < rounds_buf; i++) {
		hash_ctx_init(&ctxpool[i]);
		ctxpool[i].user_data = (void *)((uint64_t) i);
	}
	posix_memalign((void *)&mgr, 16, sizeof(HASH_CTX_MGR));
	CTX_MGR_INIT(mgr);

	printfv("Thread %i gets to wait\n", id);
	/* Thread sync */
	pthread_mutex_lock(&count_lock);
	count++;
	if (count == num_threads) {
		pthread_cond_broadcast(&count_cond);
	} else {
		pthread_cond_wait(&count_cond, &count_lock);
	}
	pthread_mutex_unlock(&count_lock);

	printfv("Thread %i is ready\n", id);
	/* hash func starts to run */
	round = 0;
	gettimeofday(&start_tv, 0);
	gettimeofday(&stop_tv, 0);
	while (secs > (stop_tv.tv_sec - start_tv.tv_sec)) {
		for (j = 0; j < rounds_buf; j += MB_BUFS) {
			for (i = 0; i < MB_BUFS; i++) {
				/* Pre mem-operation */
				if (prememcpy)
					memcpy(hash_buf[j + i], carry_buf[j + i], buflen);

				CTX_MGR_SUBMIT(mgr, &ctxpool[j + i], hash_buf[j + i], buflen,
					       HASH_ENTIRE);
			}

			/* Calculate hash digest */
			while (CTX_MGR_FLUSH(mgr)) ;
			for (i = 0; i < MB_BUFS; i++) {
				/* Post mem-operation */
				if (postmemcpy)
					memcpy(carry_buf[j + i], hash_buf[j + i], buflen);
			}
		}
		round++;

		gettimeofday(&stop_tv, 0);
	}
	printfv("thread %2i, multibuffer_func rounds %ld\n", id, round);

      out:
	free(ctxpool);
	free(mgr);
	for (j = 0; j < rounds_buf; j++) {
		free(carry_buf[j]);
		free(digests[j]);
		free(hash_buf[j]);
	}

	pthread_exit((void *)round);
}
