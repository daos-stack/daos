#include "utest_cmocka.h"

int
__wrap_PMIx_Init(pmix_proc_t *proc, pmix_info_t info[], size_t ninfo)
{
	return PMIX_SUCCESS;
}

int
__wrap_PMIx_Get(const pmix_proc_t *proc, const char key[],
		const pmix_info_t info[], size_t ninfo,
		pmix_value_t **val)
{
	assert_non_null(val);

	*val = (pmix_value_t *)calloc(sizeof(pmix_value_t), 1);

	assert_non_null(*val);

	(*val)->type = mock_type(int);

	if ((*val)->type == PMIX_UINT32)
		(*val)->data.uint32 = mock_type(int);

	return mock_type(int);
}

int
__wrap_PMIx_Publish(const pmix_info_t info[], size_t ninfo)
{
	return PMIX_SUCCESS;
}

int
__wrap_PMIx_Lookup(pmix_pdata_t data[], size_t ndata,
		   const pmix_info_t info[], size_t ninfo)
{
	data[0].value.type = mock_type(int);
	if (data[0].value.type == PMIX_UINT32)
		data[0].value.data.uint32 = mock_type(int);
	else {
		const char *str = mock_type(const char *);

		data[0].value.data.string = strdup(str);
	}

	return mock_type(int);
}

int
__wrap_PMIx_Fence(const pmix_proc_t procs[], size_t nprocs,
		  const pmix_info_t info[], size_t ninfo)
{
	return PMIX_SUCCESS;
}

int
__wrap_PMIx_Unpublish(char **keys, const pmix_info_t info[], size_t ninfo)
{
	return PMIX_SUCCESS;
}

void
__wrap_PMIx_Register_event_handler(pmix_status_t codes[], size_t ncodes,
				   pmix_info_t info[], size_t ninfo,
				   pmix_notification_fn_t evhdlr,
				   void *cbfunc,
				   void *cbdata)
{
}
