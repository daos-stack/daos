#include "crt_utils.h"


int main(void)
{
	crt_context_t ctx;
	int rc;
	
	rc = crt_init(0, CRT_FLAG_BIT_SERVER | CRT_FLAG_BIT_AUTO_SWIM_DISABLE);
	D_ASSERTF(rc == 0, "crt_init() failed\n");


	rc = crt_context_create(&ctx);
	D_ASSERTF(rc == 0, "crt_context_create() failed\n");
	DBG_PRINT("Context created\n");

	rc = crt_context_destroy(ctx, false);
	D_ASSERTF(rc == 0, "crt_context_destroy() failed\n");
	DBG_PRINT("Context destroyed\n");

	rc = crt_finalize();
	D_ASSERTF(rc == 0, "crt_finalize() failed\n");
	return 0;
}
