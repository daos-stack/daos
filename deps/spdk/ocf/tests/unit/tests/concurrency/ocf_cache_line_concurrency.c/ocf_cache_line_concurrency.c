/*
 * <tested_file_path>src/concurrency/ocf_cache_line_concurrency.c</tested_file_path>
 * <tested_function>ocf_req_async_lock_rd</tested_function>
 * <functions_to_leave>
 *   ocf_cache_line_concurrency_init
 *   ocf_cache_line_concurrency_deinit
 *   ocf_req_async_lock_rd
 *   ocf_req_async_lock_wr
 *   ocf_req_unlock_wr
 *   ocf_req_unlock_rd
 *   ocf_req_unlock
 *   ocf_cache_line_unlock_rd
 *   ocf_cache_line_unlock_wr
 *   ocf_cache_line_try_lock_rd
 *   ocf_cache_line_try_lock_wr
 *   ocf_cache_line_is_used
 *   __are_waiters
 *   __add_waiter
 *   __try_lock_wr
 *   __try_lock_rd_idle
 *   __try_lock_rd
 *   __unlock_wr
 *   __unlock_rd
 *   __try_lock_wr2wr
 *   __try_lock_wr2rd
 *   __try_lock_rd2wr
 *   __try_lock_rd2rd
 *  __lock_cache_line_wr
 *  __lock_cache_line_rd
 *  __unlock_cache_line_rd_common
 *  __unlock_cache_line_rd
 *  __unlock_cache_line_wr_common
 *  __unlock_cache_line_wr
 *  __remove_line_from_waiters_list
 *  _ocf_req_needs_cl_lock
 *  _ocf_req_trylock_rd
 *  _ocf_req_lock_rd
 *  _ocf_req_lock_wr
 *  _ocf_req_trylock_wr
 *  _req_on_lock
 *  ocf_cache_line_are_waiters
 *  ocf_cl_lock_line_needs_lock
 *  ocf_cl_lock_line_get_entry
 *  ocf_cl_lock_line_is_acting
 *  ocf_cl_lock_line_slow
 *  ocf_cl_lock_line_fast
 * </functions_to_leave>
 */

#undef static

#undef inline

#define TEST_MAX_MAP_SIZE 32

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <unistd.h>
#include <time.h>
#include "print_desc.h"

#include "ocf_concurrency.h"
#include "../ocf_priv.h"
#include "../ocf_request.h"
#include "../utils/utils_cache_line.h"
#include "../utils/utils_realloc.h"

#include "concurrency/ocf_cache_line_concurrency.c/ocf_cache_line_concurrency_generated_wraps.c"

#include "../utils/utils_alock.c"

#define LOCK_WAIT_TIMEOUT 5

int __wrap_ocf_alock_init(struct ocf_alock **self, unsigned num_entries,
  const char* name, struct ocf_alock_lock_cbs *cbs, ocf_cache_t cache)
{
	return ocf_alock_init(self, num_entries, name, cbs, cache);
}

void __wrap_ocf_alock_waitlist_remove_entry(struct ocf_alock *alock,
 struct ocf_request *req, ocf_cache_line_t entry, int i, int rw)
{
	ocf_alock_waitlist_remove_entry(alock, req, entry, i, rw);
}

void __wrap_ocf_alock_deinit(struct ocf_alock **self)
{
	ocf_alock_deinit(self);
}

void __wrap_ocf_alock_mark_index_locked(struct ocf_alock *alock,
  struct ocf_request *req, unsigned index, _Bool locked)
{
	ocf_alock_mark_index_locked(alock, req, index, locked);
}

void __wrap_ocf_alock_unlock_one_wr(struct ocf_alock *alock,
  const ocf_cache_line_t entry_idx)
{
	ocf_alock_unlock_one_wr(alock, entry_idx);
}

int __wrap_ocf_alock_lock_wr(struct ocf_alock *alock,
  struct ocf_request *req, ocf_req_async_lock_cb cmpl)
{
	return ocf_alock_lock_wr(alock, req, cmpl);
}

int __wrap_ocf_alock_lock_rd(struct ocf_alock *alock,
  struct ocf_request *req, ocf_req_async_lock_cb cmpl)
{
	return ocf_alock_lock_rd(alock, req, cmpl);
}


void __wrap_ocf_alock_unlock_one_rd(struct ocf_alock *alock,
  const ocf_cache_line_t entry)
{
	ocf_alock_unlock_one_rd(alock, entry);
}

void __wrap_ocf_alock_is_index_locked(struct ocf_alock *alock,
		  struct ocf_request *req, unsigned index)
{
	ocf_alock_is_index_locked(alock, req, index);
}

bool __wrap_ocf_alock_lock_one_wr(struct ocf_alock *alock,
		  const ocf_cache_line_t entry, ocf_req_async_lock_cb cmpl,
		  void *req, uint32_t idx)
{
	usleep(rand() % 100);
	return ocf_alock_lock_one_wr(alock, entry, cmpl, req, idx);
}

bool __wrap_ocf_alock_trylock_entry_rd_idle(struct ocf_alock *alock,
		  ocf_cache_line_t entry)
{
	return ocf_alock_trylock_entry_rd_idle(alock, entry);
}

bool __wrap_ocf_alock_lock_one_rd(struct ocf_alock *alock, const ocf_cache_line_t entry, ocf_req_async_lock_cb cmpl,
       void *req, uint32_t idx)
{
	usleep(rand() % 100);
	return ocf_alock_lock_one_rd(alock, entry, cmpl, req, idx);
}

bool __wrap_ocf_alock_waitlist_is_empty(struct ocf_alock *alock, ocf_cache_line_t entry)
{
	return ocf_alock_waitlist_is_empty(alock, entry);
}

bool __wrap_ocf_alock_trylock_one_rd(struct ocf_alock *alock, ocf_cache_line_t entry)
{
	return ocf_alock_trylock_one_rd(alock, entry);
}

bool __wrap_ocf_alock_trylock_entry_wr(struct ocf_alock *alock, ocf_cache_line_t entry)
{
	return ocf_alock_trylock_entry_wr(alock, entry);
}

void __wrap___assert_fail (const char *__assertion, const char *__file,
      unsigned int __line, const char *__function)
{
	print_message("assertion failure %s in %s:%u %s\n",
		__assertion, __file, __line, __function);
}

int __wrap_list_empty(struct list_head *l1)
{
	return l1->next == l1;
}

void __wrap_list_del(struct list_head *it)
{
	it->next->prev = it->prev;
	it->prev->next = it->next;
}

void __wrap_list_add_tail(struct list_head *it, struct list_head *l1)
{
	it->prev = l1->prev;
	it->next = l1;

	l1->prev->next = it;
	l1->prev = it;
}

void __wrap_ocf_realloc_cp(void** mem, size_t size, size_t count, size_t *limit)
{
	if (*mem)
		free(*mem);

	if (count == 0)
		return;

	*mem = malloc(size * count);
	memset(*mem, 0, size * count);

	*limit = count;
}

const char *__wrap_ocf_cache_get_name(ocf_cache_t cache)
{
	return "test";
}

int __wrap_snprintf (char *__restrict __s, size_t __maxlen,
	const char *__restrict __format, ...)
{
	va_list args;
	int ret;

	va_start(args, __format);
	ret = vsnprintf(__s, __maxlen, __format, args);
	va_end(args);

	return ret;
}

ocf_ctx_t ocf_cache_get_ctx(ocf_cache_t cache)
{
	return NULL;
}

int __wrap_ocf_log_raw(ocf_logger_t logger, ocf_logger_lvl_t lvl, const char *fmt, ...)
{
	char buf[1024];

	va_list args;
	int ret;

	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	printf(buf);

	return 0;
}

unsigned long long progress;
pthread_cond_t prog_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t prog_mutex = PTHREAD_MUTEX_INITIALIZER;

struct test_req {
	struct ocf_request r;
	struct ocf_map_info map[TEST_MAX_MAP_SIZE];
	uint8_t alock_map[TEST_MAX_MAP_SIZE];
	pthread_cond_t completion;
	pthread_mutex_t completion_mutex;
	bool finished;
};

static void req_async_lock_callback(struct ocf_request *req)
{
	struct test_req* treq = (struct test_req *)req;

	pthread_mutex_lock(&treq->completion_mutex);
	treq->finished = true;
	pthread_cond_signal(&treq->completion);
	pthread_mutex_unlock(&treq->completion_mutex);
}

bool req_lock_sync(struct ocf_cache_line_concurrency *c, struct ocf_request *req,
		int (*pfn)(struct ocf_cache_line_concurrency *c, struct ocf_request *req,
				void (*cmpl)(struct ocf_request *req)),
		volatile int *finish)
{
	struct test_req* treq = (struct test_req *)req;
	int result;
	bool timeout = false;
	struct timespec ts;

	treq->finished = false;

	result = pfn(c, req, req_async_lock_callback);
	assert(result >= 0);

	if (result == OCF_LOCK_ACQUIRED) {
		return true;
	}

	pthread_mutex_lock(&treq->completion_mutex);
	while (!treq->finished && !*finish) {
		pthread_cond_wait(&treq->completion,
			&treq->completion_mutex);
	}
	pthread_mutex_unlock(&treq->completion_mutex);

	return treq->finished;
}

struct thread_ctx
{
	pthread_t t;
	struct ocf_cache_line_concurrency *c;
	unsigned num_iterations;
	unsigned clines;
	unsigned max_io_size;
	bool timeout;
	volatile int finished;
	volatile int terminated;
	struct test_req treq;
};

void shuffle(unsigned *array, unsigned size)
{
	int i, j;
	unsigned tmp;

	for (i = size - 1; i >= 0; i--)
	{
		j = rand() % (i + 1);

		tmp = array[i];
		array[i] = array[j];
		array[j] = tmp;
	}
}

void thread(void *_ctx)
{
	struct thread_ctx *ctx = _ctx;
	struct ocf_cache_line_concurrency *c = ctx->c;
	struct ocf_request *req = &ctx->treq.r;
	unsigned i;
	unsigned cline;
	unsigned *permutation;
	bool rw;
	bool single;
	int (*lock_pfn)(struct ocf_cache_line_concurrency *c, struct ocf_request *req,
				void (*cmpl)(struct ocf_request *req));
	unsigned max_io_size = min(min(TEST_MAX_MAP_SIZE, ctx->clines), ctx->max_io_size);
	unsigned line;
	bool locked;

	ctx->treq.r.map = &ctx->treq.map;
	ctx->treq.r.alock_status = &ctx->treq.alock_map;
	pthread_cond_init(&ctx->treq.completion, NULL);
	pthread_mutex_init(&ctx->treq.completion_mutex, NULL);

	permutation = malloc(ctx->clines * sizeof(unsigned));

	for (i = 0; i < ctx->clines; i++)
		permutation[i] = i;

	i = ctx->num_iterations;
	while (i-- && !ctx->terminated)
	{
		rw = rand() % 2;
		single = (rand() % 5 == 0);

		if (!single) {
			shuffle(permutation, ctx->clines);
			req->core_line_count = (rand() % max_io_size) + 1;

			for (cline = 0;  cline < req->core_line_count; cline++) {
				req->map[cline].core_id = 0;
				req->map[cline].core_line = 0;
				req->map[cline].coll_idx = permutation[cline];
				req->map[cline].status = LOOKUP_HIT;
			}

			lock_pfn = rw ? ocf_req_async_lock_wr : ocf_req_async_lock_rd;

			if (req_lock_sync(c, req, lock_pfn, &ctx->terminated)) {
				usleep(rand() % 500);
				if (rw)
					ocf_req_unlock_wr(c, req);
				else
					ocf_req_unlock_rd(c, req);
				usleep(rand() % 500);
			}
		} else {
			line = rand() % ctx->clines;
			if (rw)
				locked = ocf_cache_line_try_lock_wr(c, line);
			else
				locked = ocf_cache_line_try_lock_rd(c, line);

			usleep(rand() % 500);

			if (locked) {
				if (rw)
					ocf_cache_line_unlock_wr(c, line);
				else
					ocf_cache_line_unlock_rd(c, line);
				usleep(rand() % 500);
			}
		}

		pthread_mutex_lock(&prog_mutex);
		progress++;
		pthread_cond_signal(&prog_cond);
		pthread_mutex_unlock(&prog_mutex);
	}

	free(permutation);
	ctx->finished = 1;
}

int cmp_map(const void *p1, const void *p2)
{
	struct ocf_map_info * m1 = *( struct ocf_map_info **)p1;
	struct ocf_map_info * m2 = *( struct ocf_map_info **)p2;

	if (m1->coll_idx > m2->coll_idx)
		return 1;
	if (m2->coll_idx > m1->coll_idx)
		return -1;

	return 0;
}

static void cctest(unsigned num_threads, unsigned num_iterations, unsigned clines,
		unsigned max_io_size)
{
	struct ocf_cache_line_concurrency *c;
	struct thread_ctx *threads;
	unsigned i, j;
	time_t t;
	char desc[1024];
	unsigned randseed = (unsigned) time(&t);
	unsigned last_progress = 0, curr_progress = 0;
	struct timespec ts;
	int result;
	bool deadlocked = false;
	unsigned timeout_s = max_io_size / 10 + 3;

	snprintf(desc, sizeof(desc), "cacheline concurrency deadlock detection "
		"threads %u iterations %u cache size %u max io size %u randseed %u\n",
		num_threads, num_iterations, clines, max_io_size, randseed);

	print_test_description(desc);

	progress = 0;
	pthread_cond_init(&prog_cond, NULL);
	pthread_mutex_init(&prog_mutex, NULL);

	srand(randseed);

	threads = malloc(num_threads * sizeof(threads[0]));
	memset(threads, 0, num_threads * sizeof(threads[0]));

	assert_int_equal(0, ocf_cache_line_concurrency_init(&c, clines, NULL));

	for (i = 0; i < num_threads; i++)
	{
		threads[i].timeout = false;
		threads[i].finished = false;
		threads[i].terminated = false;
		threads[i].c = c;
		threads[i].num_iterations = num_iterations;
		threads[i].clines = clines;
		threads[i].max_io_size = max_io_size;
		pthread_create(&threads[i].t, NULL, thread, &threads[i]);
	}

	do {
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec += timeout_s;
		do {
			last_progress = curr_progress;
			pthread_mutex_lock(&prog_mutex);
			result = pthread_cond_timedwait(&prog_cond,
				&prog_mutex, &ts);
			curr_progress = progress;
			pthread_mutex_unlock(&prog_mutex);
		} while (!result && curr_progress == last_progress);
	 } while (last_progress != curr_progress);

	for (i = 0; i < num_threads; i++)
	{
		if (!threads[i].finished) {
			print_message("deadlocked\n");
			deadlocked = true;
			break;
		}
	}

	if (!deadlocked)
		goto join;

	/* print locks on which all stuck threads are hanging */
	for (i = 0; i < num_threads; i++)
	{
		if (!threads[i].finished)
		{
			struct ocf_request *req = &threads[i].treq.r;
			unsigned num_clines = req->core_line_count;
			struct ocf_map_info **clines = malloc(num_clines *
					sizeof(*clines));
			for (j = 0; j < num_clines; j++)
			{
				clines[j] = &req->map[j];
			}

			qsort(clines, num_clines, sizeof(*clines), cmp_map);

			print_message("thread no %u\n", i);
			for (j = 0; j < num_clines; j++) {
				struct ocf_map_info *map = clines[j];
				unsigned index = map - req->map;
				const char *status = env_bit_test(index, (unsigned long*)&req->alock_status) ?
						(req->alock_rw == OCF_WRITE ? "W" : "R") : "X";
				print_message("[%u] %u %s\n", j, map->coll_idx, status);
			}

			free(clines);
		}
	}

	/* terminate all waiting threads */
	for (i = 0; i < num_threads; i++) {
		threads[i].terminated = 1;
		pthread_mutex_lock(&threads[i].treq.completion_mutex);
		pthread_cond_signal(&threads[i].treq.completion);
		pthread_mutex_unlock(&threads[i].treq.completion_mutex);
	}

join:
	assert_int_equal((int)deadlocked, 0);

	for (i = 0; i < num_threads; i++) {
		pthread_join(threads[i].t, NULL);
	}

	ocf_cache_line_concurrency_deinit(&c);

	free(threads);
}

static void ocf_req_async_lock_rd_test01(void **state)
{
	cctest(8, 10000, 16, 8);
}

static void ocf_req_async_lock_rd_test02(void **state)
{
	cctest(64, 1000, 16, 8);
}

static void ocf_req_async_lock_rd_test03(void **state)
{
	cctest(64, 1000, 128, 32);
}

static void ocf_req_async_lock_rd_test04(void **state)
{
	cctest(64, 1000, 1024, 32);
}

static void ocf_req_async_lock_rd_test05(void **state)
{
	cctest(rand() % 64, 1000, rand() % 1024, 32);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(ocf_req_async_lock_rd_test01),
		cmocka_unit_test(ocf_req_async_lock_rd_test02),
		cmocka_unit_test(ocf_req_async_lock_rd_test03),
		cmocka_unit_test(ocf_req_async_lock_rd_test04),
		cmocka_unit_test(ocf_req_async_lock_rd_test05)
	};

	print_message("Cacheline concurrency deadlock detection\n");

	return cmocka_run_group_tests(tests, NULL, NULL);
}
