
#define D_LOGFAC DD_FAC(misc)

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <dlfcn.h>
#include <pthread.h>
#include <limits.h>
#include <stdint.h>

#include <malloc.h>

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

static bool
d_parser_is_valid(d_parser_t *parser)
{
	return (parser != NULL && parser->p_magic == D_PARSER_MAGIC);
}

int
d_parser_init(d_parser_t **parser)
{
	D_ALLOC_PTR(*parser);
	if (parser == NULL)
		return -DER_NOMEM;

	D_INIT_LIST_HEAD(&(*parser)->p_handlers);
	(*parser)->p_magic = D_PARSER_MAGIC;

	return 0;
}

void
d_parser_fini(d_parser_t *parser)
{
	d_parser_handler_t *handler;

	if (!d_parser_is_valid(parser))
		return;

	while ((handler = d_list_pop_entry(&parser->p_handlers, d_parser_handler_t, ph_link)) !=
	       NULL) {
		if (handler->ph_cbs.pc_parser_fini_cb != NULL)
			handler->ph_cbs.pc_parser_fini_cb(handler->ph_id, handler->ph_arg);
		D_FREE(handler);
	}

	if (parser->p_magic != 0) {
		d_free_string(&parser->p_output);
		parser->p_magic = 0;
		D_FREE(parser);
	}
}

int
d_parser_handler_register(d_parser_t *parser, const char *id, d_parser_cbs_t *cbs, void *arg)
{
	d_parser_handler_t *handler;

	if (!d_parser_is_valid(parser))
		return -DER_INVAL;

	D_ALLOC_PTR(handler);
	if (handler == NULL)
		return -DER_NOMEM;

	handler->ph_arg = arg;
	handler->ph_cbs = *cbs;
	strncpy(handler->ph_id, id, D_PARSER_ID_MAX_LEN);
	handler->ph_id[D_PARSER_ID_MAX_LEN - 1] = 0;

	d_list_add_tail(&handler->ph_link, &parser->p_handlers);

	return 0;
}

const char *
d_parser_output_get(d_parser_t *parser)
{
	if (!d_parser_is_valid(parser))
		return "Invalid parser\n";

	if (parser->p_output.str == NULL)
		return "";

	return parser->p_output.str;
}

char *
d_parser_string_copy(d_parser_t *parser, const char *str, int *len, bool strip)
{
	int buf_nr;

	*len = 0;

	if (!d_parser_is_valid(parser))
		return "";

	struct d_parser_internal *internal = (struct d_parser_internal *)&parser->d_internal[0];

	if (internal->pi_free_buf_mask == 0) {
		d_parser_output_put(parser, "Insufficient buffers to parse input");
		return "";
	}

	buf_nr = __builtin_ffs(internal->pi_free_buf_mask);
	internal->pi_free_buf_mask ^= 1 << buf_nr;

	char *buf = &internal->pi_bufs[buf_nr].pb_buf;

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

	return buf;
}

int
d_parser_run(d_parser_t *parser, void *data, int len, d_parser_copy_cb copy_cb, void *arg)
{
	char               *buf = NULL;
	char               *id  = NULL;
	char               *end;
	d_parser_handler_t *handler;
	char               *handler_buf = NULL;
	int                 handler_len;
	int                 rc = 0;

	if (!d_parser_is_valid(parser))
		return -DER_INVAL;

	/** Extra byte allocated for possible NUL character */
	D_ALLOC_ARRAY(buf, len + 1);
	if (data == NULL)
		return -DER_NOMEM;

	rc = copy_cb(buf, len, data);
	if (rc != 0) {
		d_write_string_buffer(&parser->p_output, "Could not copy parser data: " DF_RC "\n",
				      DP_RC(rc));
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
	id = d_parser_string_copy(parser, buf, &len);
	if (len == 0) {
		d_write_string_buffer(&parser->p_output, "No type parameter given to parser\n");
		goto out;
	}

	d_list_for_each_entry(handler, &parser->p_handlers, ph_link) {
		if (strcmp(id, handler->ph_id) == 0) {
			if (handler->ph_cbs.pc_parser_run_cb != NULL) {
				handler->ph_cbs.pc_parser_run_cb(&parser->p_output, handler_buf,
								 handler_len, arg);
				goto out;
			}
		}
	}
	d_write_string_buffer(&parser->p_output, "Could not find handler for %s\n", id);

out:
	D_FREE(buf);
	return rc;
}
