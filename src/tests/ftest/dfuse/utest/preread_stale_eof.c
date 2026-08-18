/*
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Write/check helper for the dfuse pre-read stale-EOF ftest case (DAOS-18683)
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PATTERN_LEN 255

static void
patterned_bytes(unsigned char *buf, size_t size, unsigned int seed)
{
	size_t offset = seed % PATTERN_LEN;
	size_t i;

	for (i = 0; i < size; i++)
		buf[i] = ((offset + i) % PATTERN_LEN) + 1;
}

static int
do_write(const char *path, size_t size, unsigned int seed)
{
	unsigned char *buf;
	int            fd;
	ssize_t        written;
	int            rc = 0;

	buf = malloc(size);
	if (buf == NULL) {
		fprintf(stderr, "malloc(%zu) failed\n", size);
		return 1;
	}
	patterned_bytes(buf, size, seed);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
		free(buf);
		return 1;
	}

	written = write(fd, buf, size);
	if (written != (ssize_t)size) {
		fprintf(stderr, "write(%s) wrote %zd bytes, expected %zu\n", path, written, size);
		rc = 1;
	}

	/* Writeback flush errors surface at close; swallowing one here would read as corruption */
	if (close(fd) != 0) {
		fprintf(stderr, "close(%s) failed: %s\n", path, strerror(errno));
		rc = 1;
	}
	free(buf);
	return rc;
}

static void
drop_cache(int fd, const char *path)
{
	int rc = posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);

	if (rc != 0)
		fprintf(stderr, "posix_fadvise(%s) failed: %s\n", path, strerror(rc));
}

/* The corruption reads back as either a short read at a valid offset or a NUL-filled buffer */
static void
report_bad_read(const char *what, size_t offset, ssize_t got, size_t want,
		const unsigned char *data)
{
	size_t nuls = 0;

	if (got != (ssize_t)want) {
		fprintf(stderr, "%s at offset %zu returned %zd bytes, expected %zu\n", what, offset,
			got, want);
		return;
	}
	while (nuls < want && data[nuls] == 0)
		nuls++;
	if (nuls == want)
		fprintf(stderr, "%s at offset %zu returned %zu NUL bytes instead of data\n", what,
			offset, want);
	else
		fprintf(stderr, "%s at offset %zu returned wrong data\n", what, offset);
}

static int
do_check(const char *path, size_t size, unsigned int seed)
{
	unsigned char *expected;
	unsigned char *data;
	size_t         total;
	size_t         want_len;
	ssize_t        got;
	int            fd;
	pid_t          child;
	int            status;

	expected = malloc(size);
	data     = malloc(size);
	if (expected == NULL || data == NULL) {
		fprintf(stderr, "malloc(%zu) failed\n", size);
		free(expected);
		free(data);
		return 1;
	}
	patterned_bytes(expected, size, seed);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
		free(expected);
		free(data);
		return 1;
	}

	total = 0;
	while (total < size) {
		got = read(fd, data + total, size - total);
		if (got <= 0)
			break;
		total += got;
	}
	if (total != size || memcmp(data, expected, size) != 0) {
		fprintf(stderr, "sequential read-back mismatch: got %zu/%zu bytes%s\n", total, size,
			total == size ? " (content differs)" : "");
		close(fd);
		free(expected);
		free(data);
		return 3;
	}

	drop_cache(fd, path);

	want_len = size < 4096 ? size : 4096;

	got = pread(fd, data, want_len, 0);
	if (got != (ssize_t)want_len || memcmp(data, expected, want_len) != 0) {
		report_bad_read("pread", 0, got, want_len, data);
		close(fd);
		free(expected);
		free(data);
		return 1;
	}

	/* Drop the pread's pages or the child never reaches dfuse (fallback probe) */
	drop_cache(fd, path);

	child = fork();
	if (child == 0) {
		void *mapped = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

		if (mapped == MAP_FAILED) {
			fprintf(stderr, "mmap failed: %s\n", strerror(errno));
			_exit(3);
		}
		_exit(memcmp(mapped, expected, want_len) == 0 ? 0 : 1);
	}
	if (child < 0) {
		fprintf(stderr, "fork() failed: %s\n", strerror(errno));
		close(fd);
		free(expected);
		free(data);
		return 1;
	}

	if (waitpid(child, &status, 0) != child) {
		fprintf(stderr, "waitpid() failed: %s\n", strerror(errno));
		close(fd);
		free(expected);
		free(data);
		return 1;
	}

	close(fd);
	free(expected);
	free(data);

	if (WIFSIGNALED(status)) {
		fprintf(stderr, "mmap child killed by %s\n", strsignal(WTERMSIG(status)));
		return 2;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 3) {
		fprintf(stderr, "mmap child could not map the file\n");
		return 1;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
		fprintf(stderr, "mmap child read wrong content at offset 0\n");
		return 2;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		fprintf(stderr, "mmap child exited abnormally, status 0x%x\n", status);
		return 2;
	}

	return 0;
}

struct race_read {
	int            fd;
	off_t          off;
	size_t         len;
	unsigned char *buf;
	ssize_t        got;
};

static void *
race_read_fn(void *arg)
{
	struct race_read *rr = arg;

	rr->got = pread(rr->fd, rr->buf, rr->len, rr->off);
	return NULL;
}

/* Queued reads drain in arrival order; later wrong data means the drain armed handle state */
static int
do_race(const char *path, size_t size, unsigned int seed)
{
	unsigned char   *expected;
	unsigned char   *data;
	struct race_read high;
	pthread_t        thread;
	ssize_t          got;
	int              fd;
	int              rc = 0;

	if (size < 8192) {
		fprintf(stderr, "race mode needs size >= 8192\n");
		return 1;
	}

	expected = malloc(size);
	data     = malloc(size);
	if (expected == NULL || data == NULL) {
		fprintf(stderr, "malloc(%zu) failed\n", size);
		free(expected);
		free(data);
		return 1;
	}
	patterned_bytes(expected, size, seed);

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
		free(expected);
		free(data);
		return 1;
	}

	high.fd  = fd;
	high.off = (off_t)size - 4096;
	high.len = 4096;
	high.buf = data + 4096;
	high.got = -1;
	if (pthread_create(&thread, NULL, race_read_fn, &high) != 0) {
		fprintf(stderr, "pthread_create failed\n");
		close(fd);
		free(expected);
		free(data);
		return 1;
	}
	got = pread(fd, data, 4096, size - 8192);
	pthread_join(thread, NULL);

	if (got != 4096 || high.got != 4096 || memcmp(data, expected + size - 8192, 4096) != 0 ||
	    memcmp(data + 4096, expected + size - 4096, 4096) != 0) {
		fprintf(stderr, "racing reads wrong: low %zd bytes, high %zd bytes\n", got,
			high.got);
		rc = 1;
		goto out;
	}

	drop_cache(fd, path);

	/* Probe the tail offsets and zero: a stateful drain arms EOF wherever it last wrote */
	got = pread(fd, data, 4096, size - 8192);
	if (got != 4096 || memcmp(data, expected + size - 8192, 4096) != 0) {
		report_bad_read("re-read", size - 8192, got, 4096, data);
		rc = 1;
		goto out;
	}
	got = pread(fd, data, 4096, size - 4096);
	if (got != 4096 || memcmp(data, expected + size - 4096, 4096) != 0) {
		report_bad_read("re-read", size - 4096, got, 4096, data);
		rc = 1;
		goto out;
	}
	got = pread(fd, data, 4096, 0);
	if (got != 4096 || memcmp(data, expected, 4096) != 0) {
		report_bad_read("re-read", 0, got, 4096, data);
		rc = 1;
	}

out:
	close(fd);
	free(expected);
	free(data);
	return rc;
}

#define APPEND_LEN 4096

/* Reading to EOF arms the cache; the append must disarm it or the re-read sees EOF */
static int
do_append(const char *path, size_t size, unsigned int seed)
{
	unsigned char *expected;
	unsigned char *data;
	unsigned char *tail;
	ssize_t        got;
	int            fd;
	int            rc = 0;

	expected = malloc(size);
	data     = malloc(size + APPEND_LEN);
	tail     = malloc(APPEND_LEN);
	if (expected == NULL || data == NULL || tail == NULL) {
		fprintf(stderr, "malloc(%zu) failed\n", size);
		free(expected);
		free(data);
		free(tail);
		return 1;
	}
	patterned_bytes(expected, size, seed);
	patterned_bytes(tail, APPEND_LEN, seed + 7);

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "open(%s) failed: %s\n", path, strerror(errno));
		free(expected);
		free(data);
		free(tail);
		return 1;
	}

	got = read(fd, data, size + APPEND_LEN);
	if (got != (ssize_t)size || memcmp(data, expected, size) != 0) {
		fprintf(stderr, "read-back before append: got %zd/%zu bytes\n", got, size);
		rc = 3;
		goto out;
	}

	got = pwrite(fd, tail, APPEND_LEN, size);
	if (got != APPEND_LEN) {
		fprintf(stderr, "append pwrite returned %zd: %s\n", got, strerror(errno));
		rc = 1;
		goto out;
	}

	got = pread(fd, data, APPEND_LEN, size);
	if (got != APPEND_LEN || memcmp(data, tail, APPEND_LEN) != 0) {
		report_bad_read("re-read of appended bytes", size, got, APPEND_LEN, data);
		rc = 1;
	}

out:
	if (close(fd) != 0 && rc == 0) {
		fprintf(stderr, "close(%s) failed: %s\n", path, strerror(errno));
		rc = 1;
	}
	free(expected);
	free(data);
	free(tail);
	return rc;
}

static void
usage(const char *prog)
{
	fprintf(stderr, "usage: %s write|check|race|append <path> <size> <seed>\n", prog);
}

int
main(int argc, char *argv[])
{
	size_t       size;
	unsigned int seed;

	if (argc != 5) {
		usage(argv[0]);
		return 1;
	}

	size = strtoul(argv[3], NULL, 10);
	seed = strtoul(argv[4], NULL, 10);

	if (strcmp(argv[1], "write") == 0)
		return do_write(argv[2], size, seed);
	if (strcmp(argv[1], "check") == 0)
		return do_check(argv[2], size, seed);
	if (strcmp(argv[1], "race") == 0)
		return do_race(argv[2], size, seed);
	if (strcmp(argv[1], "append") == 0)
		return do_append(argv[2], size, seed);

	usage(argv[0]);
	return 1;
}
