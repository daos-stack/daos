
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
d_strip(char *str, int *len)
{
	while (*len > 0 && isspace(str[0])) {
		str++;
		(*len)--;
	}

	char *end = &str[*len - 1];

	while ((*len) > 0 && isspace(*end)) {
		*(end--) = '\0';
		(*len)--;
	}

	return str;
}

int
d_parser_run(d_parser_t *parser, void *data, int len, d_parser_copy_cb copy_cb)
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

	d_reset_string(&parser->p_output);

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
	id = d_strip(buf, &len);
	if (len == 0) {
		d_write_string_buffer(&parser->p_output, "No type parameter given to parser\n");
		goto out;
	}

	d_list_for_each_entry(handler, &parser->p_handlers, ph_link) {
		if (strcmp(id, handler->ph_id) == 0) {
			if (handler->ph_cbs.pc_parser_run_cb != NULL) {
				handler->ph_cbs.pc_parser_run_cb(&parser->p_output, handler_buf,
								 handler_len, handler->ph_arg);
				goto out;
			}
		}
	}
	d_write_string_buffer(&parser->p_output, "Could not find handler for %s\n", id);

out:
	D_FREE(buf);
	return rc;
}
