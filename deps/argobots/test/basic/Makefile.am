# -*- Mode: Makefile; -*-
#
# See COPYRIGHT in top-level directory.
#

TESTS = \
	init_finalize \
	xstream_create \
	xstream_revive \
	xstream_affinity \
	xstream_barrier \
	xstream_rank \
	xstream_set_main_sched \
	thread_create \
	thread_create2 \
	thread_create3 \
	thread_create4 \
	thread_create_on_xstream \
	thread_revive \
	thread_attr \
	thread_attr2 \
	thread_yield \
	thread_yield2 \
	thread_yield_to \
	thread_exit \
	thread_self_suspend_resume \
	thread_get_last_xstream \
	thread_get_func_arg \
	thread_migrate \
	thread_data \
	thread_data2 \
	thread_id \
	task_create \
	task_create_on_xstream \
	task_revive \
	task_data \
	task_data2 \
	thread_task \
	thread_task_arg \
	thread_task_num \
	sched_basic \
	sched_basic_wait \
	sched_on_thread \
	sched_prio \
	sched_randws \
	sched_set_main \
	sched_stack \
	sched_config \
	sched_user_ws \
	pool_config \
	pool_custom \
	pool_user_def \
	sync_no_contention \
	main_sched \
	mutex \
	mutex_prio \
	mutex_recursive \
	mutex_spinlock \
	mutex_static \
	mutex_unlock_se \
	cond_test \
	cond_join \
	cond_signal_in_main \
	cond_static \
	cond_timedwait \
	rwlock_writer_excl \
	rwlock_reader_writer_excl \
	rwlock_reader_incl \
	future_create \
	eventual_create \
	eventual_static \
	eventual_test \
	barrier \
	self_exit_to \
	self_rank_id \
	self_resume_to \
	self_suspend_to \
	self_type \
	self_yield_to \
	ext_thread \
	ext_thread2 \
	ext_thread_barrier \
	ext_thread_cond \
	ext_thread_eventual \
	ext_thread_future \
	ext_thread_join \
	ext_thread_mutex \
	ext_thread_rwlock \
	stack_guard \
	timer \
	info_print \
	info_print_stack \
	info_query \
	info_stackdump \
	info_stackdump2 \
	unit \
	error

XFAIL_TESTS =
if ABT_CONFIG_DISABLE_EXT_THREAD
XFAIL_TESTS += self_type ext_thread ext_thread2 ext_thread_barrier \
               ext_thread_cond ext_thread_eventual ext_thread_future \
               ext_thread_join ext_thread_mutex ext_thread_rwlock
endif
if ABT_CONFIG_DISABLE_ERROR_CHECK
XFAIL_TESTS += self_type
endif

check_PROGRAMS = $(TESTS)
noinst_PROGRAMS = $(TESTS)

include $(top_srcdir)/test/Makefile.mk

init_finalize_SOURCES = init_finalize.c
xstream_create_SOURCES = xstream_create.c
xstream_revive_SOURCES = xstream_revive.c
xstream_affinity_SOURCES = xstream_affinity.c
xstream_barrier_SOURCES = xstream_barrier.c
xstream_rank_SOURCES = xstream_rank.c
xstream_set_main_sched_SOURCES = xstream_set_main_sched.c
thread_create_SOURCES = thread_create.c
thread_create2_SOURCES = thread_create2.c
thread_create3_SOURCES = thread_create3.c
thread_create4_SOURCES = thread_create4.c
thread_create_on_xstream_SOURCES = thread_create_on_xstream.c
thread_revive_SOURCES = thread_revive.c
thread_attr_SOURCES = thread_attr.c
thread_attr2_SOURCES = thread_attr2.c
thread_yield_SOURCES = thread_yield.c
thread_yield2_SOURCES = thread_yield2.c
thread_yield_to_SOURCES = thread_yield_to.c
thread_exit_SOURCES = thread_exit.c
thread_self_suspend_resume_SOURCES = thread_self_suspend_resume.c
thread_get_last_xstream_SOURCES = thread_get_last_xstream.c
thread_get_func_arg_SOURCES = thread_get_func_arg.c
thread_migrate_SOURCES = thread_migrate.c
thread_data_SOURCES = thread_data.c
thread_data2_SOURCES = thread_data2.c
thread_id_SOURCES = thread_id.c
task_create_SOURCES = task_create.c
task_create_on_xstream_SOURCES = task_create_on_xstream.c
task_revive_SOURCES = task_revive.c
task_data_SOURCES = task_data.c
task_data2_SOURCES = task_data2.c
thread_task_SOURCES = thread_task.c
thread_task_arg_SOURCES = thread_task_arg.c
thread_task_num_SOURCES = thread_task_num.c
sched_basic_SOURCES = sched_basic.c
sched_basic_wait_SOURCES = sched_basic_wait.c
sched_on_thread_SOURCES = sched_on_thread.c
sched_prio_SOURCES = sched_prio.c
sched_randws_SOURCES = sched_randws.c
sched_set_main_SOURCES = sched_set_main.c
sched_stack_SOURCES = sched_stack.c
sched_config_SOURCES = sched_config.c
sched_user_ws_SOURCES = sched_user_ws.c
pool_config_SOURCES = pool_config.c
pool_custom_SOURCES = pool_custom.c
pool_user_def_SOURCES = pool_user_def.c
sync_no_contention_SOURCES = sync_no_contention.c
main_sched_SOURCES = main_sched.c
mutex_SOURCES = mutex.c
mutex_prio_SOURCES = mutex_prio.c
mutex_recursive_SOURCES = mutex_recursive.c
mutex_spinlock_SOURCES = mutex_spinlock.c
mutex_static_SOURCES = mutex_static.c
mutex_unlock_se_SOURCES = mutex_unlock_se.c
cond_test_SOURCES = cond_test.c
cond_join_SOURCES = cond_join.c
cond_signal_in_main_SOURCES = cond_signal_in_main.c
cond_static_SOURCES = cond_static.c
cond_timedwait_SOURCES = cond_timedwait.c
rwlock_writer_excl_SOURCES = rwlock_writer_excl.c
rwlock_reader_writer_excl_SOURCES = rwlock_reader_writer_excl.c
rwlock_reader_incl_SOURCES = rwlock_reader_incl.c
future_create_SOURCES = future_create.c
eventual_create_SOURCES = eventual_create.c
eventual_static_SOURCES = eventual_static.c
eventual_test_SOURCES = eventual_test.c
barrier_SOURCES = barrier.c
self_exit_to_SOURCES = self_exit_to.c
self_rank_id_SOURCES = self_rank_id.c
self_resume_to_SOURCES = self_resume_to.c
self_suspend_to_SOURCES = self_suspend_to.c
self_type_SOURCES = self_type.c
self_yield_to_SOURCES = self_yield_to.c
ext_thread_SOURCES = ext_thread.c
ext_thread2_SOURCES = ext_thread2.c
ext_thread_barrier_SOURCES = ext_thread_barrier.c
ext_thread_cond_SOURCES = ext_thread_cond.c
ext_thread_eventual_SOURCES = ext_thread_eventual.c
ext_thread_future_SOURCES = ext_thread_future.c
ext_thread_join_SOURCES = ext_thread_join.c
ext_thread_mutex_SOURCES = ext_thread_mutex.c
ext_thread_rwlock_SOURCES = ext_thread_rwlock.c
stack_guard_SOURCES = stack_guard.c
timer_SOURCES = timer.c
info_print_SOURCES = info_print.c
info_print_stack_SOURCES = info_print_stack.c
info_query_SOURCES = info_query.c
info_stackdump_SOURCES = info_stackdump.c
info_stackdump2_SOURCES = info_stackdump2.c
unit_SOURCES = unit.c
error_SOURCES = error.c

testing:
	./init_finalize
	./xstream_create
	./xstream_revive
	./xstream_affinity
	./xstream_barrier
	./xstream_rank
	./xstream_set_main_sched
	./thread_create
	./thread_create2
	./thread_create3
	./thread_create4
	./thread_create_on_xstream
	./thread_revive
	./thread_attr
	./thread_attr2
	./thread_yield
	./thread_yield2
	./thread_yield_to
	./thread_exit
	./thread_self_suspend_resume
	./thread_get_last_xstream
	./thread_get_func_arg
	./thread_migrate
	./thread_data
	./thread_data2
	./thread_id
	./task_create
	./task_create_on_xstream
	./task_revive
	./task_data
	./task_data2
	./thread_task
	./thread_task_arg
	./thread_task_num
	./sched_basic
	./sched_basic_wait
	./sched_on_thread
	./sched_prio
	./sched_randws
	./sched_set_main
	./sched_stack
	./sched_config
	./sched_user_ws
	./pool_config
	./pool_custom
	./pool_user_def
	./sync_no_contention
	./main_sched
	./mutex
	./mutex_prio
	./mutex_recursive
	./mutex_spinlock
	./mutex_static
	./mutex_unlock_se
	./cond_test
	./cond_join
	./cond_signal_in_main
	./cond_static
	./cond_timedwait
	./rwlock_writer_excl
	./rwlock_reader_writer_excl
	./rwlock_reader_incl
	./future_create
	./eventual_create
	./eventual_static
	./eventual_test
	./barrier
	./self_exit_to
	./self_rank_id
	./self_resume_to
	./self_suspend_to
	./self_type
	./self_yield_to
	./ext_thread
	./ext_thread2
	./ext_thread_barrier
	./ext_thread_cond
	./ext_thread_eventual
	./ext_thread_future
	./ext_thread_join
	./ext_thread_mutex
	./ext_thread_rwlock
	./stack_guard
	./timer
	./info_print
	./info_print_stack
	./info_query
	./info_stackdump
	./info_stackdump2
	./unit
	./error
