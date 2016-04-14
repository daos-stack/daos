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

#if defined(HWLOC) || defined(HWLOC2)
#include <hwloc.h>
#define call_hwloc() hwloc_get_api_version()
#else
#define call_hwloc() (void)0
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
	call_hwloc();
	call_openpa();
}
