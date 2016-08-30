/* Copyright (c) 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
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
