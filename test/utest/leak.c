#include <stdlib.h>

int leak(void)
{
	void *leak_var = malloc(10);
#ifdef NEGATIVE_TEST
	free(leak_var);
#endif
	return 0;
}

int main(void)
{
	return leak();
}
