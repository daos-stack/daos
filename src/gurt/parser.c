
#define D_LOGFAC DD_FAC(misc)

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <malloc.h>

#include <gurt/common.h>
#include <gurt/parser.h>

/**
 * To reduce likelihood of errors, we allocate enough buffer space to cover
 * existing use cases.
 */
#define D_PARSER_MAX_IO_SIZE  2048
#define D_PARSER_MAX_BUF_SIZE 256
#define D_PARSER_BUF_NR       16

struct d_parser_handler {
	d_list_t          ph_link;
	d_parser_run_cb_t ph_run_cb;
	char              ph_id[D_PARSER_ID_MAX_LEN];
};

struct d_parser_buf {
	char pb_buf[D_PARSER_MAX_BUF_SIZE];
};

struct d_parser_internal {
	char                pi_input[D_PARSER_MAX_IO_SIZE];
	char                pi_output[D_PARSER_MAX_IO_SIZE];
	struct d_parser_buf pi_bufs[D_PARSER_BUF_NR];
	uint32_t            pi_out_offset;
	uint32_t            pi_free_buf_mask;
};

#define D_PARSER_MAGIC 0xbaadf00d

static inline struct d_parser_internal *
d_parser2internal(d_parser_t *parser)
{
	return (struct d_parser_internal *)&(parser->p_internal[0]);
}

static bool
d_parser_is_valid(d_parser_t *parser)
{
	return (parser != NULL && parser->p_magic == D_PARSER_MAGIC);
}

static inline void
reset_parser(struct d_parser_internal *internal)
{
	internal->pi_out_offset    = 0;
	internal->pi_output[0]     = 0;
	internal->pi_free_buf_mask = (1 << D_PARSER_BUF_NR) - 1;
}

int
d_parser_init(d_parser_t **parser_ptr)
{
	d_parser_t *parser;
	D_ALLOC(parser, sizeof(*parser) + sizeof(struct d_parser_internal));
	if (parser == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&parser->p_handlers);
	parser->p_magic = D_PARSER_MAGIC;
	reset_parser(d_parser2internal(parser));

	*parser_ptr = parser;

	return 0;
}

void
d_parser_fini(d_parser_t *parser)
{
	struct d_parser_handler *handler;

	if (!d_parser_is_valid(parser))
		return;

	while ((handler = d_list_pop_entry(&parser->p_handlers, struct d_parser_handler,
					   ph_link)) != NULL) {
		D_FREE(handler);
	}

	if (parser->p_magic != 0) {
		parser->p_magic = 0;
		D_FREE(parser);
	}
}

int
d_parser_handler_register(d_parser_t *parser, const char *id, d_parser_run_cb_t run_cb)
{
	struct d_parser_handler *handler;

	if (!d_parser_is_valid(parser))
		return -DER_INVAL;

	D_ALLOC_PTR(handler);
	if (handler == NULL)
		return -DER_NOMEM;

	handler->ph_run_cb = run_cb;
	strncpy(handler->ph_id, id, D_PARSER_ID_MAX_LEN);
	handler->ph_id[D_PARSER_ID_MAX_LEN - 1] = 0;

	d_list_add_tail(&handler->ph_link, &parser->p_handlers);

	return 0;
}

const char *
d_parser_output_get(d_parser_t *parser, int *len)
{
	if (!d_parser_is_valid(parser))
		return "Invalid parser\n";

	struct d_parser_internal *internal = d_parser2internal(parser);
	*len                               = internal->pi_out_offset;
	return internal->pi_output;
}

char *
d_parser_string_copy(d_parser_t *parser, const char *str, int *len, bool strip)
{
	int buf_nr;

	*len = 0;

	if (!d_parser_is_valid(parser))
		return "";

	struct d_parser_internal *internal = d_parser2internal(parser);

	buf_nr = __builtin_ffs(internal->pi_free_buf_mask);
	if (buf_nr == 0) {
		d_parser_output_put(parser, "Insufficient buffers to parse input");
		return "";
	}
	internal->pi_free_buf_mask ^= 1 << (buf_nr - 1);

	char *buf = &internal->pi_bufs[buf_nr].pb_buf[0];

	/** Value can be truncated but it is invalid */
	strncpy(buf, str, D_PARSER_MAX_BUF_SIZE);
	buf[D_PARSER_MAX_BUF_SIZE - 1] = 0;

	if (!strip)
		goto out;

	*len = strlen(buf);

	while (*len > 0 && isspace(buf[0])) {
		buf++;
		(*len)--;
	}

	char *end = &buf[*len - 1];

	while ((*len) > 0 && isspace(*end)) {
		*(end--) = '\0';
		(*len)--;
	}
out:
	return buf;
}

int
d_parser_run(d_parser_t *parser, void *data, int len, d_parser_copy_cb copy_cb, void *arg)
{
	char                    *buf = NULL;
	char                    *id  = NULL;
	char                    *end;
	struct d_parser_handler *handler;
	char                    *handler_buf = NULL;
	int                      handler_len;
	int                      rc = 0;

	if (!d_parser_is_valid(parser))
		return -DER_INVAL;

	/* Just truncate the input */
	if (len >= D_PARSER_MAX_IO_SIZE) {
		d_parser_output_put(parser, "Can't parse a buffer larger than %d bytes\n",
				    D_PARSER_MAX_IO_SIZE - 1);
		return 0;
	}

	struct d_parser_internal *internal = d_parser2internal(parser);

	reset_parser(internal);
	buf = &internal->pi_input[0];

	rc = copy_cb(buf, len, data);
	if (rc != 0) {
		d_parser_output_put(parser, "Could not copy parser data: " DF_RC "\n", DP_RC(rc));
		goto out;
	}

	buf[len] = '\0';
	end      = strchr(buf, '\n');
	if (end != NULL) {
		*end        = 0;
		handler_buf = end + 1;
		handler_len = len - (handler_buf - buf);
		len         = end - buf;
	} else {
		handler_buf = &buf[len];
		handler_len = 0;
	}
	id = d_parser_string_copy(parser, buf, &len, true);
	if (len == 0) {
		d_parser_output_put(parser, "No type parameter given to parser\n");
		goto out;
	}

	d_list_for_each_entry(handler, &parser->p_handlers, ph_link) {
		if (strcmp(id, handler->ph_id) == 0) {
			if (handler->ph_run_cb != NULL) {
				handler->ph_run_cb(parser, handler_buf, handler_len, arg);
				goto out;
			}
		}
	}
	d_parser_output_put(parser, "Could not find handler for %s\n", id);

out:
	return rc;
}

void
d_parser_output_put(d_parser_t *parser, const char *fmt, ...)
{
	if (!d_parser_is_valid(parser))
		return;

	struct d_parser_internal *internal  = d_parser2internal(parser);
	int                       remaining = sizeof(internal->pi_output) - internal->pi_out_offset;

	/** We truncate the output to avoid allocations at parsing time */
	if (remaining <= 1)
		return;

	va_list ap;

	va_start(ap, fmt);
	int n = vsnprintf(&internal->pi_output[internal->pi_out_offset], remaining, fmt, ap);
	va_end(ap);

	if (n < 0)
		return;

	internal->pi_out_offset += remaining > n ? n : remaining;
}
