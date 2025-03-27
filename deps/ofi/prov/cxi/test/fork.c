/*
 * Copyright (c) 2023 Hewlett Packard Enterprise Development LP
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <ctype.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <pthread.h>

#include "libcxi/libcxi.h"
#include "cxip.h"
#include "cxip_test_common.h"

#define SECRET 0xFFU
#define XFER_SIZE 257U
#define INIT_BUF_VALUE 0xAAU
#define INIT_BUF_OFFSET 127U
#define TGT_BUF_VALUE 0xFFU
#define TGT_BUF_OFFSET 3215U
#define RKEY 0x1U
#define MAP_HUGE_2MB (21 << MAP_HUGE_SHIFT)

/* Needs to be marked volatile to prevent hangs due to compiler optimization. */
static volatile bool child_process_block = true;

static void signal_handler(int sig)
{
	child_process_block = false;
}

static void fork_test_runner(bool odp, bool huge_page, bool fork_safe)
{
	long page_size;
	uint8_t *buf;
	uint8_t *init_buf;
	uint8_t *tgt_buf;
	int ret;
	struct fid_mr *mr;
	int status;
	struct fi_cq_tagged_entry cqe;
	pid_t pid;
	int i = 0;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	uint64_t rkey;
	bool again;

	if (odp) {
		ret = setenv("FI_CXI_FORCE_ODP", "1", 1);
		cr_assert_eq(ret, 0, "Failed to set FI_CXI_FORCE_ODP %d",
			     -errno);
	}

	if (fork_safe) {
		ret = setenv("CXI_FORK_SAFE", "1", 1);
		cr_assert_eq(ret, 0, "Failed to set CXI_FORK_SAFE %d", -errno);

		if (huge_page) {
			ret = setenv("CXI_FORK_SAFE_HP", "1", 1);
			cr_assert_eq(ret, 0, "Failed to set CXI_FORK_SAFE %d",
				     -errno);
		}
	}

	cxit_setup_msg();

	signal(SIGUSR1, signal_handler);

	/* Single map is used for page aliasing with child process and RDMA. */
	if (huge_page) {
		page_size = 2 * 1024 * 1024;
		flags |= MAP_HUGETLB | MAP_HUGE_2MB;
	} else {
		page_size = sysconf(_SC_PAGESIZE);
	}

	buf = mmap(NULL, page_size, PROT_READ | PROT_WRITE, flags, -1, 0);
	cr_assert(buf != MAP_FAILED, "mmap failed");

	memset(buf, 0, page_size);

	/* This secret is passed to the child process. Child process will verify
	 * it receives this secret.
	 */
	buf[0] = SECRET;
	init_buf = buf + INIT_BUF_OFFSET;
	tgt_buf = buf + TGT_BUF_OFFSET;

	/* Register the buffer. The behavior of the child buffer depends upon
	 * the following
	 * - If CXI_FORK_SAFE is set and copy-on-fork kernel support does not
	 *   exist, madvise(MADV_DONTFORK) will be issued against the page.
	 *   This will cause the child to segfault.
	 * - If CXI_FORK_SAFE is set and copy-on-fork kernel support exists,
	 *   madvise(MADV_DONTFORK) will NOT be issued against the page. The
	 *   child process will get its data and the parent process will
	 *   not have data corruption.
	 * - If ODP is not used and kernel copy-on-fork is not supported, the
	 *   child process will get its data, and the parent process will have
	 *   data corruption.
	 * - If ODP is not used and the kernel supports copy-on-fork, the child
	 *   process will get its data, and the parent process will not have
	 *   data corruption.
	 * - If ODP is used, the child process will get its data, and the parent
	 *   process will not have data corruption.
	 */
	ret = fi_mr_reg(cxit_domain, tgt_buf, XFER_SIZE, FI_REMOTE_WRITE, 0,
			RKEY, 0, &mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed %d", ret);

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind failed %d", ret);

	ret = fi_mr_enable(mr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable failed %d", ret);

	rkey = fi_mr_key(mr);

	again = true;
	do {
		pid = fork();
		if (pid >= 0) {
			again = false;
			break;
		}

		cr_assert_eq(errno, EAGAIN, "fork() failed: %d", errno);
	} while (again);

	if (pid == 0) {
		while (child_process_block)
			sched_yield();

		/* If CXI_FORK_SAFE is set (i.e. fork_safe is true) and
		 * kernel copy-on-fork does not exist, this will segfault.
		 */
		if (buf[0] == SECRET)
			_exit(EXIT_SUCCESS);

		/* This should never happen. */
		_exit(EXIT_FAILURE);
	}

	/* Writing these buffers will trigger COW if copy-on-fork
	 * kernel support does not exist. If that is the case then unless
	 * madvise(MADV_DONTFORK) was called, parent process will get a new
	 * page.
	 */
	memset(init_buf, INIT_BUF_VALUE, XFER_SIZE);
	memset(tgt_buf, TGT_BUF_VALUE, XFER_SIZE);

	ofi_sfence();

	/* Unblock the child process. */
	kill(pid, SIGUSR1);

	ret = fi_write(cxit_ep, init_buf, XFER_SIZE, NULL, cxit_ep_fi_addr, 0,
		       rkey, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_write failed %d", ret);

	ret = cxit_await_completion(cxit_tx_cq, &cqe);
	cr_assert_eq(ret, 1, "fi_cq_read failed %d", ret);

	validate_tx_event(&cqe, FI_RMA | FI_WRITE, NULL);

	if (cxil_is_copy_on_fork() || odp || fork_safe) {
		for (i = 0; i < XFER_SIZE; i++)
			cr_assert_eq(init_buf[i], tgt_buf[i], "data corruption with fork");
	} else {
		for (i = 0; i < XFER_SIZE; i++)
			cr_assert_neq(init_buf[i], tgt_buf[i], "Missing data corruption with fork");
	}

	waitpid(pid, &status, 0);

	if (!cxil_is_copy_on_fork() && fork_safe) {
		cr_assert_eq(WIFSIGNALED(status), true, "Child was not terminated by signal: is_exit=%d exit=%d is_sig=%d sig=%d",
			     WIFEXITED(status), WEXITSTATUS(status),
			     WIFSIGNALED(status), WTERMSIG(status));
		cr_assert_eq(WTERMSIG(status), SIGSEGV, "Child signal was not SIGSEGV");
	} else {
		cr_assert_eq(WIFEXITED(status), true, "Child was not terminated by exit: is_exit=%d exit=%d is_sig=%d sig=%d",
			     WIFEXITED(status), WEXITSTATUS(status),
			     WIFSIGNALED(status), WTERMSIG(status));
		cr_assert_eq(WEXITSTATUS(status), EXIT_SUCCESS, "Child process had data corruption");
	}

	fi_close(&mr->fid);
	munmap(buf, page_size);

	cxit_teardown_msg();
}

TestSuite(fork, .timeout = CXIT_DEFAULT_TIMEOUT);

/* No ODP, no fork safe variables, and system page size. On kernels before 5.12,
 * parent process should have data corruption. Child process should not have
 * data corruption and should not segfault.
 */
Test(fork, page_aliasing_no_odp_no_fork_safe_system_page_size)
{
	fork_test_runner(false, false, false);
}

/* ODP, no fork safe variables, and system page size. Parent process should not
 * have data corruption regardless of kernel version. Child process should not
 * have data corruption and should not segfault.
 */
Test(fork, page_aliasing_odp_no_fork_safe_system_page_size)
{
	fork_test_runner(true, false, false);
}

/* No ODP, no fork safe variables, and system page size. Parent process should
 * not have data corruption regardless of kernel version. Child process should
 * segfault if copy-on-fork kernel support does not exist (The parent would
 * have called madvise MADV_DONTFORK if that is the case).
 */
Test(fork, page_aliasing_no_odp_fork_safe_system_page_size)
{
	fork_test_runner(false, false, true);
}

/* No ODP, no fork safe variables, and 2MiB page size. On kernels before 5.12,
 * parent process should have data corruption. Child process should not have
 * data corruption and should not segfault.
 */
Test(fork, page_aliasing_no_odp_no_fork_safe_huge_page)
{
	fork_test_runner(false, true, false);
}

/* ODP, no fork safe variables, and 2MiB page size. Parent process should not
 * have data corruption regardless of kernel version. Child process should not
 * have data corruption and should not segfault.
 */
Test(fork, page_aliasing_odp_no_fork_safe_huge_page)
{
	fork_test_runner(true, true, false);
}

/* No ODP, with fork safe variables, and 2MiB page size. Parent process should
 * not have data corruption regardless of kernel version. Child process should
 * segfault if the kernel does not support copy-on-fork (since the parent
 * would have called MADV_DONTFORK on virtual address range).
 */
Test(fork, page_aliasing_no_odp_fork_safe_huge_page)
{
	fork_test_runner(false, true, true);
}

static volatile bool block_threads = true;

static void *child_memory_free_thread_runner(void *context)
{
	bool huge_page = (bool)context;
	long page_size;
	uint8_t *buf;
	int ret;
	struct fid_mr *mr;
	int status;
	pid_t pid;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	bool again;

	while (block_threads)
		sched_yield();

	/* Single map is used for page aliasing with child process and RDMA. */
	if (huge_page) {
		page_size = 2 * 1024 * 1024;
		flags |= MAP_HUGETLB | MAP_HUGE_2MB;
	} else {
		page_size = sysconf(_SC_PAGESIZE);
	}

	buf = mmap(NULL, page_size, PROT_READ | PROT_WRITE, flags, -1, 0);
	cr_assert(buf != MAP_FAILED, "mmap failed");

	memset(buf, 0, page_size);

	ret = fi_mr_reg(cxit_domain, buf, XFER_SIZE, FI_REMOTE_WRITE, 0,
			gettid(), 0, &mr, NULL);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_reg failed %d", ret);

	/* MR reg will result in cxil_map() being called. On kernels < 5.12,
	 * libcxi will call MADV_DONTFORK on the range. For the purposes of this
	 * test, we want the child to munmap this buffer to see if it deadlocks
	 * in the MR cache. Thus, we need to undo the MADV_DONTFORK.
	 */
	if (!cxil_is_copy_on_fork()) {
		ret = madvise(buf, page_size, MADV_DOFORK);
		cr_assert_eq(ret, 0, "madvise failed %d", ret);
	}

	ret = fi_mr_bind(mr, &cxit_ep->fid, 0);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_bind failed %d", ret);

	ret = fi_mr_enable(mr);
	cr_assert_eq(ret, FI_SUCCESS, "fi_mr_enable failed %d", ret);

	again = true;
	do {
		pid = fork();
		if (pid >= 0) {
			again = false;
			break;
		}

		cr_assert_eq(errno, EAGAIN, "fork() failed: %d", errno);
	} while (again);

	if (pid == 0) {
		munmap(buf, page_size);
		_exit(EXIT_SUCCESS);
	}

	waitpid(pid, &status, 0);

	cr_assert_eq(WIFEXITED(status), true, "Child was not terminated by exit: is_exit=%d exit=%d is_sig=%d sig=%d",
		     WIFEXITED(status), WEXITSTATUS(status),
		     WIFSIGNALED(status), WTERMSIG(status));
	cr_assert_eq(WEXITSTATUS(status), EXIT_SUCCESS, "Child process had data corruption");

	fi_close(&mr->fid);
	munmap(buf, page_size);

	return NULL;
}

#define THREAD_MAX 256U

static void child_memory_free_runner(bool huge_page, int thread_count)
{
	pthread_t threads[THREAD_MAX];
	int i;
	int ret;

	cr_assert(thread_count <= THREAD_MAX);

	/* For kernels < 5.12, CXI_FORK_SAFE needs to be set. If not set, the
	 * control event queue buffers would be subjected to copy-on-write. This
	 * may result in the parent threads deadlocking.
	 */
	ret = setenv("CXI_FORK_SAFE", "1", 1);
	cr_assert_eq(ret, 0, "Failed to set CXI_FORK_SAFE %d", -errno);

	if (huge_page) {
		ret = setenv("CXI_FORK_SAFE_HP", "1", 1);
		cr_assert_eq(ret, 0, "Failed to set CXI_FORK_SAFE %d", -errno);
	}

	cxit_setup_msg();

	for (i = 0; i < thread_count; i++) {
		ret = pthread_create(&threads[i], NULL,
				     child_memory_free_thread_runner,
				     (void *)huge_page);
		cr_assert(ret == 0);
	}

	block_threads = false;

	for (i = 0; i < thread_count; i++)
		pthread_join(threads[i], NULL);

	cxit_teardown_msg();
}

/* The objective of this test is to see if child processes can deadlock on the
 * MR cache lock if threads are forking while other threads are doing memory
 * registration.
 */
Test(fork, child_memory_free_system_page_size)
{
	child_memory_free_runner(false, 16);
}

Test(fork, child_memory_free_huge_page_size)
{
	child_memory_free_runner(true, 16);
}
