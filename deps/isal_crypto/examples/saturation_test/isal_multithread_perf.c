/**
 * @file isal_multithread_perf.c
 * @brief It is used to verify high speed algorithm saturation issue
 * @details
 *	usage: taskset -c <cpu_indexs1,cpu_index2,...> isal_multithread_perf -m <algorithm name> -n <thread num>
 *	eg: taskset -c 0-9,20-29 ./isal_multithread_perf -m md5_mb -n 10
 */

#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "isal_multithread_perf.h"

alg_method algs[] = {
	{"md5", md5_ossl_func, MD5_MAX_LANES}
	,
	{"md5_mb", md5_mb_func, MD5_MAX_LANES}
	,
	{"sha1", sha1_ossl_func, SHA1_MAX_LANES}
	,
	{"sha1_mb", sha1_mb_func, SHA1_MAX_LANES}
	,
	{"sha256", sha256_ossl_func, SHA256_MAX_LANES}
	,
	{"sha256_mb", sha256_mb_func, SHA256_MAX_LANES}
	,
	{"sha512", sha512_ossl_func, SHA512_MAX_LANES}
	,
	{"sha512_mb", sha512_mb_func, SHA512_MAX_LANES}
	,
	{"cbc_128_dec", cbc_128_dec_func, 1}
	,
	{"cbc_192_dec", cbc_192_dec_func, 1}
	,
	{"cbc_256_dec", cbc_256_dec_func, 1}
	,
	{"xts_128_enc", xts_128_enc_func, 1}
	,
	{"xts_256_enc", xts_256_enc_func, 1}
	,
	{"gcm_128_enc", gcm_128_enc_func, 1}
	,
	{"gcm_256_enc", gcm_256_enc_func, 1}
	,

	{NULL, NULL}
};

/* Global parameters*/
long long run_secs = 10;
uint32_t num_threads = 2;
uint32_t buflen = 32 * 1024;
uint32_t prememcpy = 0;
uint32_t postmemcpy = 0;
char *method = "md5_mb";

/* Global thread sync */
pthread_mutex_t count_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t count_cond = PTHREAD_COND_INITIALIZER;
volatile uint32_t count = 0;

int verbose = 0;

void usage(char *appname)
{
	int i = 0;
	printf("Usage: %s -n num_threads\n", appname);
	printf("\t-v verbose output\n"
	       "\t-t time to run(secs)\n"
	       "\t-n number of algorithm threads\n"
	       "\t-l len of each buffer(KB)\n"
	       "\t-a memory copy before algorithm -- 1 do(default); 0 not do\n"
	       "\t-b memory copy after algorithm -- 1 do(default); 0 not do\n"
	       "\t-m method of algorithm:");
	for (i = 0; algs[i].name != NULL; i++)
		printf("  %s", algs[i].name);
	printf("\n");

}

void notice(char *appname, alg_method * alg_choose_p)
{
	int i = 0;
	printf("%s starts to run\n", appname);
	printf("\tverbose output is %d\n"
	       "\truntime is %lld(secs)\n"
	       "\tnumber of algorithm threads is %d\n"
	       "\tlen of each buffer(KB) is %d\n"
	       "\tmemory copy before algorithm is %d\n"
	       "\tmemory copy after algorithm is %d\n"
	       "\tmethod of algorithm is %s\n", verbose, run_secs, num_threads, buflen / 1024,
	       prememcpy, postmemcpy, alg_choose_p->name);
}

int main(int argc, char **argv)
{
	int i = 0;
	int opt;
	char *optstring = "t:n:m:l:a:b:v";
	int32_t *id = NULL, ret = 0;
	alg_method alg_choose;
	pthread_t *clients = NULL;
	uint64_t count = 0, sum = 0;
	uint32_t rounds_buf;

	while ((opt = getopt(argc, argv, optstring)) != -1) {
		switch (opt) {
		case 't':
			run_secs = atol(optarg);
			if (run_secs <= 0) {
				usage(argv[0]);
				exit(-1);
			}
			break;
		case 'n':
			num_threads = atoi(optarg);
			if (num_threads <= 0) {
				usage(argv[0]);
				exit(-1);
			}
			break;
		case 'm':
			method = optarg;
			break;
		case 'l':
			buflen = atoi(optarg) * 1024;
			if (buflen <= 0) {
				usage(argv[0]);
				exit(-1);
			}
			break;
		case 'a':
			prememcpy = atoi(optarg);
			if (prememcpy != 0 && prememcpy != 1) {
				usage(argv[0]);
				exit(-1);
			}
			break;
		case 'b':
			postmemcpy = atoi(optarg);
			if (postmemcpy != 0 && postmemcpy != 1) {
				usage(argv[0]);
				exit(-1);
			}
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(argv[0]);
			exit(0);
		}
	}

	/* Check method str and set algorithm_func */
	for (i = 0; algs[i].name != NULL; i++) {
		if (!strcmp(method, algs[i].name)) {
			alg_choose = algs[i];
			break;
		}
	}
	if (algs[i].name == NULL) {
		usage(argv[0]);
		exit(-1);
	}

	notice(argv[0], &alg_choose);
	rounds_buf = alg_choose.rounds_nbuf;

	clients = (pthread_t *) calloc(num_threads + 1, sizeof(pthread_t));
	id = (int32_t *) calloc(num_threads + 1, sizeof(int32_t));

	printf("Start %i threads, use %s function\n", num_threads, alg_choose.name);

	for (i = 0; i < num_threads; i++) {
		id[i] = i;

		ret =
		    pthread_create(&clients[i], NULL, alg_choose.thread_func, (void *)&id[i]);

		if (ret != 0) {
			printf("Failed to create thread %i: %s", i, strerror(ret));
			exit(-1);
		}
		printfv("Thread %i is created\n", i);
	}

	for (i = 0; i < num_threads; i++) {
		pthread_join(clients[i], (void *)&count);
		sum += count;
	}
	double loop_unit = ((double)buflen) * rounds_buf / run_secs / 1024 / 1024;
	printf("Sum of rounds is %ld\n"
	       "Average throughput(MB/s) is %.2f\n"
	       "Total throughput(MB/s) is %.2f\n",
	       sum, (double)sum / i * loop_unit, (double)sum * loop_unit);

	exit(0);
}
