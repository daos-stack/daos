
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "isal_multithread_perf.h"

struct aes_context {
	int const bits;
	int (*const preproc)(struct aes_context * pCtx);
	void (*const processor)(struct aes_context * pCtx, char *plaintext,
				char *ciphertext, uint64_t len);
	void (*const postproc)(struct aes_context * pCtx);
};

#define rounds_buf 2		/* first one is plain text, second is cipher text */

static uint64_t aes_thread_func(int32_t id, struct aes_context *pCtx)
{
	uint32_t i = 0, j = 0;
	char *aes_buf[rounds_buf] = { NULL };	/* aes buf is used to do checksum compute */
	char *carry_buf[rounds_buf] = { NULL };	/* carry buf is used to do memory movement */
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

		aes_buf[j] = (char *)calloc((size_t)buflen, 1);
		if (aes_buf[j] == NULL) {
			printf("calloc failed test aborted\n");
			goto out;
		}

		/* Create the random data */
		for (i = 0; i < buflen; i += 1024) {
			carry_buf[j][i] = i % 256;
			aes_buf[j][i] = i % 256;
		}
	}

	if (pCtx->preproc(pCtx)) {
		printf("preproc failed test aborted\n");
		goto out;
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
		/* Pre mem-operation */
		if (prememcpy)
			memcpy(aes_buf[0], carry_buf[0], buflen);

		/* Calculate checksum */
		pCtx->processor(pCtx, aes_buf[0], aes_buf[1], buflen);

		/* Post mem-operation */
		if (postmemcpy)
			memcpy(carry_buf[1], aes_buf[1], buflen);

		round++;

		gettimeofday(&stop_tv, 0);
	}
	printfv("thread %2i, aes_func rounds %ld\n", id, round);

      out:
	pCtx->postproc(pCtx);

	for (j = 0; j < rounds_buf; j++) {
		free(carry_buf[j]);
		free(aes_buf[j]);
	}

	return round;
}

/*
 * facilities for AES-CBC
 */
static unsigned char const ic[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
	0x0e, 0x0f
};

void mk_rand_data(uint8_t * data, uint32_t size)
{
	unsigned int i;
	for (i = 0; i < size; i++) {
		*data++ = rand();
	}
}

/* thread functions for cbc dec */
struct cbc_context {
	struct aes_context base;
	uint8_t *iv;
	uint8_t key[CBC_256_BITS];
	struct cbc_key_data *key_data;
};

static int cbc_dec_pre(struct aes_context *p)
{
	struct cbc_context *pCtx = (struct cbc_context *)p;

	posix_memalign((void **)&pCtx->iv, 16, (CBC_IV_DATA_LEN));
	posix_memalign((void **)&pCtx->key_data, 16, (sizeof(*pCtx->key_data)));

	if ((NULL == pCtx->iv) || (NULL == pCtx->key_data))
		return 1;

	mk_rand_data(pCtx->key, sizeof(pCtx->key));
	memcpy(pCtx->iv, ic, CBC_IV_DATA_LEN);
	aes_cbc_precomp(pCtx->key, pCtx->base.bits, pCtx->key_data);

	return 0;
}

static void cbc_dec_post(struct aes_context *p)
{
	struct cbc_context *pCtx = (struct cbc_context *)p;

	free(pCtx->iv);
	free(pCtx->key_data);

	return;
}

static void cbc_dec_proc(struct aes_context *p, char *plaintext, char *ciphertext,
			 uint64_t len)
{
	struct cbc_context *pCtx = (struct cbc_context *)p;

	if (pCtx->base.bits == 128)
		aes_cbc_dec_128(ciphertext, pCtx->iv, pCtx->key_data->dec_keys, plaintext,
				len);
	else if (pCtx->base.bits == 192)
		aes_cbc_dec_192(ciphertext, pCtx->iv, pCtx->key_data->dec_keys, plaintext,
				len);
	else if (pCtx->base.bits == 256)
		aes_cbc_dec_256(ciphertext, pCtx->iv, pCtx->key_data->dec_keys, plaintext,
				len);
	else {
		printf("unsupported cbc encryption bits %d\n", pCtx->base.bits);
		exit(1);
	}

	return;
}

void *cbc_128_dec_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct cbc_context ctx =
	    { {128, cbc_dec_pre, cbc_dec_proc, cbc_dec_post}, NULL, {0}, NULL };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

void *cbc_192_dec_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct cbc_context ctx =
	    { {192, cbc_dec_pre, cbc_dec_proc, cbc_dec_post}, NULL, {0}, NULL };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

void *cbc_256_dec_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct cbc_context ctx =
	    { {256, cbc_dec_pre, cbc_dec_proc, cbc_dec_post}, NULL, {0}, NULL };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

/*
 * thread functions for xts enc
 */
struct xts_content {
	struct aes_context base;
	unsigned char key1[16 * 2];
	unsigned char key2[16 * 2];
	unsigned char tinit[16];
};

static int xts_enc_pre(struct aes_context *p)
{
	struct xts_content *pCtx = (struct xts_content *)p;

	mk_rand_data(pCtx->key1, pCtx->base.bits / 8);
	mk_rand_data(pCtx->key2, pCtx->base.bits / 8);
	mk_rand_data(pCtx->tinit, sizeof(pCtx->tinit));

	return 0;
}

static void xts_enc_post(struct aes_context *p)
{
	return;
}

static void xts_enc_proc(struct aes_context *p, char *plaintext, char *ciphertext,
			 uint64_t len)
{
	struct xts_content *pCtx = (struct xts_content *)p;

	if (pCtx->base.bits == 128)
		XTS_AES_128_enc(pCtx->key2, pCtx->key1, pCtx->tinit, len, plaintext,
				ciphertext);
	else if (pCtx->base.bits == 256)
		XTS_AES_256_enc(pCtx->key2, pCtx->key1, pCtx->tinit, len, plaintext,
				ciphertext);
	else {
		printf("unsupported xts encryption bits %d\n", pCtx->base.bits);
		exit(1);
	}

	return;
}

void *xts_128_enc_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct xts_content ctx =
	    { {128, xts_enc_pre, xts_enc_proc, xts_enc_post}, {0}, {0}, {0} };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

void *xts_256_enc_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct xts_content ctx =
	    { {256, xts_enc_pre, xts_enc_proc, xts_enc_post}, {0}, {0}, {0} };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

/*
 * thread functions for gcm enc
 */
struct gcm_context {
	struct aes_context base;
	uint8_t *key;
	unsigned char *iv;
	unsigned char *aad;
	unsigned char *gcm_tag;
	struct gcm_key_data gkey;
	struct gcm_context_data gctx;
};

static int gcm_enc_pre(struct aes_context *p)
{
	uint8_t const IVend[] = GCM_IV_END_MARK;

	struct gcm_context *pCtx = (struct gcm_context *)p;

	pCtx->key = malloc(pCtx->base.bits / 8);
	pCtx->iv = malloc(GCM_IV_LEN);
	pCtx->gcm_tag = malloc(MAX_TAG_LEN);
	pCtx->aad = malloc(AAD_LENGTH);

	mk_rand_data(pCtx->aad, AAD_LENGTH);

	mk_rand_data(pCtx->iv, GCM_IV_LEN);
	memcpy(&pCtx->iv[GCM_IV_END_START], IVend, sizeof(IVend));

	mk_rand_data(pCtx->key, pCtx->base.bits / 8);
	if (pCtx->base.bits == 128)
		aes_gcm_pre_128(pCtx->key, &pCtx->gkey);
	else
		aes_gcm_pre_256(pCtx->key, &pCtx->gkey);

	return 0;
}

static void gcm_enc_post(struct aes_context *p)
{
	struct gcm_context *pCtx = (struct gcm_context *)p;

	free(pCtx->key);
	free(pCtx->iv);
	free(pCtx->gcm_tag);
	free(pCtx->aad);

	return;
}

static void gcm_enc_proc(struct aes_context *p, char *plaintext, char *ciphertext,
			 uint64_t len)
{
	struct gcm_context *pCtx = (struct gcm_context *)p;

	if (pCtx->base.bits == 128)
		aes_gcm_enc_128(&pCtx->gkey, &pCtx->gctx, ciphertext, plaintext, len, pCtx->iv,
				pCtx->aad, AAD_LENGTH, pCtx->gcm_tag, MAX_TAG_LEN);
	else if (pCtx->base.bits == 256)
		aes_gcm_enc_256(&pCtx->gkey, &pCtx->gctx, ciphertext, plaintext, len, pCtx->iv,
				pCtx->aad, AAD_LENGTH, pCtx->gcm_tag, MAX_TAG_LEN);
	else {
		printf("unsupported gcm encryption bits %d\n", pCtx->base.bits);
		exit(1);
	}

	return;
}

void *gcm_128_enc_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct gcm_context ctx =
	    { {128, gcm_enc_pre, gcm_enc_proc, gcm_enc_post}, NULL, NULL, NULL, NULL, {0} };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}

void *gcm_256_enc_func(void *arg)
{
	int32_t id = *((int *)arg);
	uint64_t round = -1;

	struct gcm_context ctx =
	    { {256, gcm_enc_pre, gcm_enc_proc, gcm_enc_post}, NULL, NULL, NULL, NULL, {0} };

	round = aes_thread_func(id, &ctx.base);

	pthread_exit((void *)round);
}
