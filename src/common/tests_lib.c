/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC	DD_FAC(tests)

/**
 * Test suite helper functions.
 */
#include <daos/common.h>
#include <daos/object.h>
#include <daos/tests_lib.h>
#include <daos.h>
#include <gurt/debug.h>

#define DTS_OCLASS_DEF		OC_RP_XSF

static uint32_t obj_id_gen	= 1;
static uint64_t int_key_gen	= 1;

daos_obj_id_t
dts_oid_gen(uint16_t oclass, uint8_t ofeats, unsigned seed)
{
	daos_obj_id_t	oid;
	uint64_t	hdr;

	if (oclass == 0)
		oclass = DTS_OCLASS_DEF;

	hdr = seed;
	hdr <<= 32;

	/* generate a unique and not scary long object ID */
	oid.lo	= obj_id_gen++;
	oid.lo	|= hdr;
	oid.hi	= rand() % 100;
	daos_obj_generate_id(&oid, ofeats, oclass, 0);

	return oid;
}

daos_unit_oid_t
dts_unit_oid_gen(uint16_t oclass, uint8_t ofeats, uint32_t shard)
{
	daos_unit_oid_t	uoid;

	uoid.id_pub	= dts_oid_gen(oclass, ofeats, time(NULL));
	uoid.id_shard	= shard;
	uoid.id_pad_32	= 0;

	return uoid;
}

void
dts_key_gen(char *key, unsigned int key_len, const char *prefix)
{
	memset(key, 0, key_len);
	if (prefix == NULL)
		memcpy(key, &int_key_gen, sizeof(int_key_gen));
	else
		snprintf(key, key_len, "%s-"DF_U64, prefix, int_key_gen);
	int_key_gen++;
}

void
dts_buf_render(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = 'a' + randv;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

void
dts_buf_render_uppercase(char *buf, unsigned int buf_len)
{
	int	nr = 'z' - 'a' + 1;
	int	i;

	for (i = 0; i < buf_len - 1; i++) {
		int randv = rand() % (2 * nr);

		if (randv < nr)
			buf[i] = ('a' + randv) - 32;
		else
			buf[i] = 'A' + (randv - nr);
	}
	buf[i] = '\0';
}

#define DTS_LINE_SIZE	1024

void
dts_freeline(char *line)
{
	D_FREE(line);
}

/**
 * Read a command line from stdin, save trouble if we don't have libreadline
 */
char *
dts_readline(const char *prompt)
{
	char	*line;
	char	*cur;
	bool	 eof;

	D_ALLOC(line, DTS_LINE_SIZE);
	if (!line)
		return NULL;

	if (prompt) {
		fprintf(stdout, "%s", prompt);
		fflush(stdout);
	}

	cur = line;
	eof = false;
	while (1) {
		int	c;

		c = fgetc(stdin);
		if (c == EOF) {
			if (ferror(stdin) || feof(stdin))
				goto out_free;

			eof = true;
			break;
		}

		if (c == '\n')
			break;

		*cur++ = (char)c;
		if (cur - line >= DTS_LINE_SIZE) {
			fprintf(stderr, "line is too long\n");
			goto out_free;
		}
	}
	*cur = '\0';
	if (eof && strlen(line) == 0)
		goto out_free;

	return line;
 out_free:
	dts_freeline(line);
	return NULL;
}

int
dts_cmd_parser(struct option *opts, const char *prompt,
	       int (*cmd_func)(char opc, char *args))
{
	char	*line = NULL;
	int	 rc;

	for (rc = 0; rc == 0;) {
		char	*args = NULL;
		char	*cmd;
		char	 opc;
		int	 i;

		if (line)
			dts_freeline(line);

		line = dts_readline(prompt);
		if (!line)
			break;

		if (strlen(line) == 0)
			continue; /* empty line */

		cmd = daos_str_trimwhite(line);

		for (i = 0, opc = 0;; i++) {
			struct option *opt;

			opt = &opts[i];
			if (opt->name == NULL) {
				opc = -1;
				break;
			}

			if (strncasecmp(opt->name, cmd, strlen(opt->name)))
				continue;

			/* matched a command */
			opc = (char)opt->val;
			if (opt->has_arg) {
				args = line + strlen(opt->name);
				args = daos_str_trimwhite(args);
			} else {
				args = NULL;
			}
			break;
		}

		if (opc == -1) {
			D_PRINT("Unknown command string %s, try \"help\"\n",
				 cmd);
			continue;
		}

		rc = cmd_func(opc, args);
		if (rc != 0)
			break;
	}

	if (line)
		dts_freeline(line);

	return rc;
}

static void
rand_iarr_swap(void *array, int a, int b)
{
	uint64_t	*iarray = array;
	uint64_t	 tmp;

	tmp = iarray[a];
	iarray[a] = iarray[b];
	iarray[b] = tmp;
}

static daos_sort_ops_t rand_iarr_ops = {
	.so_swap	= rand_iarr_swap,
};

uint64_t *
dts_rand_iarr_alloc(int nr)
{
	uint64_t	*array;

	D_ALLOC_ARRAY(array, nr);
	if (!array)
		return NULL;

	return array;
}

void
dts_rand_iarr_set(uint64_t *array, int nr, int base, bool shuffle)
{
	int		 i;

	for (i = 0; i < nr; i++)
		array[i] = base + i;

	if (shuffle)
		daos_array_shuffle((void *)array, nr, &rand_iarr_ops);
}

uint64_t *
dts_rand_iarr_alloc_set(int nr, int base, bool shuffle)
{
	uint64_t	*array;

	array = dts_rand_iarr_alloc(nr);
	if (!array)
		return NULL;

	dts_rand_iarr_set(array, nr, base, shuffle);
	return array;
}

void
dts_reset_key(void)
{
	int_key_gen = 1;
}

void
dts_log(const char *msg, const char *file, const char *func, int line,
		uint64_t py_logfac)
{
	int logfac = 0;

	switch (py_logfac) {
	case 0:
		logfac = DB_ANY;
		break;
	case 1:
		logfac = DLOG_INFO;
		break;
	case 2:
		logfac = DLOG_WARN;
		break;
	case 3:
		logfac = DLOG_ERR;
		break;
	}

	int mask = d_log_check(logfac | D_LOGFAC);

	if (mask)
		d_log(mask, "%s:%d %s() %s", file, line, func, msg);
}

static void
v_dts_sgl_init_with_strings_repeat(d_sg_list_t *sgl, uint32_t repeat,
				   uint32_t count, char *d,
				   va_list valist)
{
	char *arg = d;
	int i, j;

	d_sgl_init(sgl, count);
	for (i = 0; i < count; i++) {
		size_t arg_len = strlen(arg);

		char *buf = NULL;
		size_t buf_len = arg_len * repeat + 1; /** +1 for NULL
							* Terminator
							*/
		D_ALLOC(buf, buf_len);
		D_ASSERT(buf != 0);
		for (j = 0; j < repeat; j++)
			memcpy(buf + j * arg_len, arg, arg_len);
		buf[buf_len - 1] = '\0';

		sgl->sg_iovs[i].iov_buf = buf;
		sgl->sg_iovs[i].iov_buf_len = sgl->sg_iovs[i].iov_len = buf_len;

		arg = va_arg(valist, char *);
	}
}

void
dts_sgl_init_with_strings(d_sg_list_t *sgl, uint32_t count, char *d, ...)
{
	va_list valist;

	va_start(valist, d);
	v_dts_sgl_init_with_strings_repeat(sgl, 1, count, d, valist);
	va_end(valist);
}

void
dts_sgl_init_with_strings_repeat(d_sg_list_t *sgl, uint32_t repeat,
				 uint32_t count, char *d, ...)
{
	va_list valist;

	va_start(valist, d);
	v_dts_sgl_init_with_strings_repeat(sgl, repeat, count, d, valist);
	va_end(valist);
}
