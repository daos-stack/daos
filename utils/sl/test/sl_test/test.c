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
#if defined(SL_PROJECT1)
#include <sl_project1.h>
#define call_sl1() sl_project1()
#else
#define call_sl1() (void)0
#endif

#if defined(SL_PROJECT2)
#include <sl_project2.h>
#define call_sl2() sl_project2()
#else
#define call_sl2() (void)0
#endif

#if defined(SL_PROJECT3)
#include <sl_project3.h>
#define call_sl3() sl_project3()
#else
#define call_sl3() (void)0
#endif

#if defined(SL_PROJECT4)
#include <sl_project4.h>
#define call_sl4() sl_project4()
#else
#define call_sl4() (void)0
#endif

#if defined(OPENPA) || defined(OPENPA2)
#include <opa_queue.h>
void call_openpa(void)
{
	OPA_Queue_info_t head;

	OPA_Queue_init(&head);
}
#else
#define call_openpa() (void)0
#endif

int main(void)
{
	call_sl1();
	call_sl2();
	call_sl3();
	call_sl4();
	call_openpa();
	return 0;
}
