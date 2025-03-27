
#ifndef ISAL_MULTITHREAD_PERF_H_
#define ISAL_MULTITHREAD_PERF_H_

#include "isa-l_crypto.h"

/* multibuffer hash */
void *md5_ossl_func(void *arg);
void *md5_mb_func(void *arg);
void *sha1_ossl_func(void *arg);
void *sha1_mb_func(void *arg);
void *sha256_ossl_func(void *arg);
void *sha256_mb_func(void *arg);
void *sha512_ossl_func(void *arg);
void *sha512_mb_func(void *arg);

/* aes */
void *cbc_128_dec_func(void *arg);
void *cbc_192_dec_func(void *arg);
void *cbc_256_dec_func(void *arg);
void *xts_128_enc_func(void *arg);
void *xts_256_enc_func(void *arg);
#define AAD_LENGTH   16
void *gcm_128_enc_func(void *arg);
void *gcm_256_enc_func(void *arg);


typedef struct {
	char *name;
	void *(*thread_func) (void *arg);
	uint32_t rounds_nbuf;	/* bufs number of one processing round */
} alg_method;


/* Global parameters*/
extern long long run_secs;
extern uint32_t num_threads;
extern uint32_t buflen;
extern uint32_t prememcpy;
extern uint32_t postmemcpy;

extern pthread_mutex_t count_lock;
extern pthread_cond_t count_cond;
extern volatile uint32_t count;

extern int verbose;
#define printfv(format, args...) { \
	if (verbose) \
		printf (format, ##args); \
}

#endif /* ISAL_MULTITHREAD_PERF_H_ */
