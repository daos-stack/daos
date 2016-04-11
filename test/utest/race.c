#include <pthread.h>
int global;
#ifdef NEGATIVE_TEST
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#endif


void *start(void *arg)
{
#ifdef NEGATIVE_TEST
	pthread_mutex_lock(&lock);
#endif
	global++;
#ifdef NEGATIVE_TEST
	pthread_mutex_unlock(&lock);
#endif
	return NULL;
}

int main(void)
{
	pthread_t other_thread;

	pthread_create(&other_thread, NULL,
		       start, NULL);
#ifdef NEGATIVE_TEST
	pthread_mutex_lock(&lock);
#endif
	global++;
#ifdef NEGATIVE_TEST
	pthread_mutex_unlock(&lock);
#endif
	pthread_join(other_thread, NULL);
	return 0;
}
