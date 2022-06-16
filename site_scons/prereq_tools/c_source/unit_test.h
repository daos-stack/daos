/**
 * (C) Copyright 2017-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <time.h>
#include <stdbool.h>

#include <cmocka.h>
#include <mimick.h>


#ifndef UNIT_TEST_H
#define UNIT_TEST_H

#define M_FUNC(A, B) A##B

#ifdef CREATE_MOCKS
#define CREATE_MOCK_FUNC(ret_type, function_name, typed_params, params)	\
ret_type(*M_FUNC(function_name, _mock)) typed_params = NULL;		\
ret_type function_name typed_params {					\
	if (M_FUNC(function_name, _mock) != NULL) {			\
		return M_FUNC(function_name, _mock) params;		\
	}								\
	else {								\
		return (ret_type)0;					\
	}								\
}
#else
#define CREATE_MOCK_FUNC(ret_type, function_name, typed_params, params)	\
ret_type (*M_FUNC(function_name, _mock)) typed_params
#endif

#define CLEAR_MOCKS()							\
clear_mocked_functions()
extern void clear_mocked_functions(void);

enum MOCK_TYPE {
    MT_SIMPLE,
    MT_MIMICK
};

extern void add_mocked_function(void **, void *, enum MOCK_TYPE);

#define MOCK_FUNC(function_name, mock_function)				\
add_mocked_function((void **)&M_FUNC(function_name, _mock),	\
			 mock_function, MT_SIMPLE)

#define MOCK_STATIC_FUNC(function_name, mock_function)			\
extern void *function_name;						\
add_mocked_function(&function_name, mock_function, MT_SIMPLE)

/*
 * Mock a function that is actually linked in by the linker, particular useful
 * for things like calling calloc
 */
#define MOCK_LINKED_FUNC(function_name, mock_function)			\
add_mocked_function((void **)&function_name, mock_function, MT_MIMICK)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/*
 * The form of this macros is
 * UNIT_TEST(test_function, setup_function, teardown_function)
 * setup_function and teardown_function are optional
 * Acceptable forms are
 * UNIT_TEST(test_function)
 * UNIT_TEST(test_function, setup_function)
 * UNIT_TEST(test_function,, teardown_function)
 * UNIT_TEST(test_function, setup_function, teardown_function)
 */
#undef UNIT_TEST
#define UNIT_TEST(unit_test, ...) \
void unit_test(void **state)

#undef GLOBAL_SETUP_FUNCTION
#define GLOBAL_SETUP(setup_function) \
int setup_function(void **state)

#undef GLOBAL_TEARDOWN
#define GLOBAL_TEARDOWN(teardown_function) \
int teardown_function(void **state)


struct _cmocka_tests {
    const char *group_name;
    struct CMUnitTest *tests;
    int number_of_tests;
    void *setup;
    void *teardown;
};

extern const struct _cmocka_tests *cmocka_tests;

extern bool verbose_unit_test_output;

extern int test_lib_main(int argc,
			 char *argv[],
			 struct _cmocka_tests *cmocka_tests);

extern char *assert_message(char *file,
			    int line,
			    int a,
			    int b,
			    char *message,
			    ...);

#define UNPACK_ARGS(...) __VA_ARGS__

#define TESTS_TO_RUN(_group_name, _tests, _setup, _teardown)		\
const struct _cmocka_tests local_cmoaka_tests = {			\
.group_name = _group_name,						\
.tests = _tests,							\
.number_of_tests = ARRAY_SIZE(_tests),					\
.setup = _setup,							\
.teardown = _teardown};							\
const struct _cmocka_tests *cmocka_tests = &local_cmoaka_tests


/*
 * Define some extra assert macros to allow for increased debugging
 */

/**
 * @defgroup cmocka_asserts Assert Macros
 * @ingroup cmocka
 *
 * This is a set of useful assert macros like the standard C libary's
 * assert(3) macro.
 *
 * On an assertion failure a cmocka assert macro will write the failure to the
 * standard error stream and signal a test failure. Due to limitations of the C
 * language the general C standard library assert() and cmocka's assert_true()
 * and assert_false() macros can only display the expression that caused the
 * assert failure. cmocka's type specific assert macros, assert_{type}_equal()
 * and assert_{type}_not_equal(), display the data that caused the assertion
 * failure which increases data visibility aiding debugging of failing test
 * cases.
 *
 * @{
 */

#ifdef DOXYGEN
/**
 * @brief Assert that the given expression is true.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if expression is false (i.e., compares equal to
 * zero).
 *
 * @param[in]  expression  The expression to evaluate.
 * @param[in]  message     The error message to display on failure
 * @param[in]  ... printf parameters for the message
 *
 * @see assert_int_equal()
 * @see assert_string_equal()
 */
void assert_true_msg(scalar expression, char *message, ...);
#else
#define assert_true_msg(c, ...)						\
{									\
	int _c = cast_to_largest_integral_type(c);			\
	_assert_true(_c,						\
		     #c,						\
		     assert_message(__FILE__, __LINE__, _c, _c, __VA_ARGS__),\
		     __LINE__);						\
}
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the given expression is false.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if expression is true.
 *
 * @param[in]  expression  The expression to evaluate.
 * @param[in]  message     The error message to display on failure
 * @param[in]  ... printf parameters for the message
 *
 *
 * @see assert_int_equal()
 * @see assert_string_equal()
 */
void assert_false_msg(scalar expression, char *message, ...);
#else
#define assert_false_msg(c, ...)					\
{									\
	int _c = !cast_to_largest_integral_type(c);			\
	_assert_true(_c,						\
		     #c,						\
		     assert_message(__FILE__, __LINE__, _c, _c, __VA_ARGS__),\
		     __LINE__);						\
}
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the return_code is greater than or equal to 0.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the return code is smaller than 0. If the function
 * you check sets an errno if it fails you can pass it to the function and
 * it will be printed as part of the error message.
 *
 * @param[in]  rc       The return code to evaluate.
 *
 * @param[in]  error    Pass errno here or 0.
 */
void assert_return_code_msg(int rc, int error);
#else
#define assert_return_code_msg(rc, error)				\
    _assert_return_code(cast_to_largest_integral_type(rc),		\
			sizeof(rc),					\
			cast_to_largest_integral_type(error),		\
			#rc, __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the given pointer is non-NULL.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the pointer is non-NULL.
 *
 * @param[in]  pointer  The pointer to evaluate.
 *
 * @see assert_null()
 */
void assert_non_null_msg(void *pointer);
#else
#define assert_non_null_msg(c)						\
	_assert_true(cast_ptr_to_largest_integral_type(c), #c,		\
		     __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the given pointer is NULL.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the pointer is non-NULL.
 *
 * @param[in]  pointer  The pointer to evaluate.
 *
 * @see assert_non_null()
 */
void assert_null_msg(void *pointer);
#else
#define assert_null_msg(c)						\
	_assert_true(!(cast_ptr_to_largest_integral_type(c)), #c,	\
		     __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given pointers are equal.
 *
 * The function prints an error message and terminates the test by calling
 * fail() if the pointers are not equal.
 *
 * @param[in]  a	The first pointer to compare.
 *
 * @param[in]  b	The pointer to compare against the first one.
 */
void assert_ptr_equal_msg(void *a, void *b);
#else
#define assert_ptr_equal_msg(a, b)					\
	_assert_int_equal(cast_ptr_to_largest_integral_type(a),		\
			  cast_ptr_to_largest_integral_type(b),		\
			  __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given pointers are not equal.
 *
 * The function prints an error message and terminates the test by calling
 * fail() if the pointers are equal.
 *
 * @param[in]  a	The first pointer to compare.
 *
 * @param[in]  b	The pointer to compare against the first one.
 */
void assert_ptr_not_equal_msg(void *a, void *b);
#else
#define assert_ptr_not_equal_msg(a, b)					\
	_assert_int_not_equal(cast_ptr_to_largest_integral_type(a),	\
			      cast_ptr_to_largest_integral_type(b),	\
			      __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the command executed did not return an error.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the integers are not equal.
 *
 * @param[in]  a  The return value to test.
 *
 * @param[in]  message     The error message to display on failure
 *
 * @param[in]  ... printf parameters for the message
 */
void assert_success_msg(int a, char *message, ...);
#else
#define assert_success_msg(a, ...)					\
{									\
	int _a = cast_to_largest_integral_type(a);			\
	_assert_int_equal(_a,						\
			  _a,						\
			  assert_message(__FILE__, __LINE__, _a, _a,	\
					 __VA_ARGS__),			\
			  __LINE__);					\
}
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the command executed did not return an error.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the integers are not equal.
 *
 * @param[in]  a  The return value to test.
 *
 */
void assert_success(int a);
#else
#define assert_success(a)						\
	_assert_int_equal(cast_to_largest_integral_type(a),		\
			  0,						\
			  __FILE__,					\
			  __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given integers are equal.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the integers are not equal.
 *
 * @param[in]  a  The first integer to compare.
 *
 * @param[in]  b  The integer to compare against the first one.
 *
 * @param[in]  message     The error message to display on failure
 *
 * @param[in]  ... printf parameters for the message
 */
void assert_int_equal_msg_msg(int a, int b char *message, ...);
#else
#define assert_int_equal_msg(a, b, ...)					\
{									\
	int _a = (long int)(a);						\
	int _b = (long int)(b);						\
	_assert_int_equal(_a,						\
			  _b,						\
			  assert_message(__FILE__, __LINE__, _a, _b,	\
					 __VA_ARGS__),			\
			  __LINE__);					\
}
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given integers are not equal.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the integers are equal.
 *
 * @param[in]  a  The first integer to compare.
 *
 * @param[in]  b  The integer to compare against the first one.
 *
 * @see assert_int_equal()
 */
void assert_int_not_equal_msg(int a, int b);
#else
#define assert_int_not_equal_msg(a, b)					\
	_assert_int_not_equal(cast_to_largest_integral_type(a),		\
			      cast_to_largest_integral_type(b),		\
			      __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given strings are equal.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the strings are not equal.
 *
 * @param[in]  a  The string to check.
 *
 * @param[in]  b  The other string to compare.
 */
void assert_string_equal_msg(const char *a, const char *b);
#else
#define assert_string_equal_msg(a, b)					\
	_assert_string_equal((const char *)(a), (const char *)(b), __FILE__,\
			     __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given strings are not equal.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the strings are equal.
 *
 * @param[in]  a  The string to check.
 *
 * @param[in]  b  The other string to compare.
 */
void assert_string_not_equal_msg(const char *a, const char *b);
#else
#define assert_string_not_equal_msg(a, b)				\
	_assert_string_not_equal((const char *)(a), (const char *)(b),	\
				 __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given areas of memory are equal, otherwise fail.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the memory is not equal.
 *
 * @param[in]  a  The first memory area to compare
 *		(interpreted as unsigned char).
 *
 * @param[in]  b  The second memory area to compare
 *		(interpreted as unsigned char).
 *
 * @param[in]  size  The first n bytes of the memory areas to compare.
 */
void assert_memory_equal_msg(const void *a, const void *b, size_t size);
#else
#define assert_memory_equal_msg(a, b, size, message, args)		\
	_assert_memory_equal((const void *)(a), (const void *)(b),	\
			     size, __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the two given areas of memory are not equal.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if the memory is equal.
 *
 * @param[in]  a  The first memory area to compare
 *		(interpreted as unsigned char).
 *
 * @param[in]  b  The second memory area to compare
 *		(interpreted as unsigned char).
 *
 * @param[in]  size  The first n bytes of the memory areas to compare.
 */
void assert_memory_not_equal_msg(const void *a, const void *b, size_t size);
#else
#define assert_memory_not_equal_msg(a, b, size)				\
	_assert_memory_not_equal((const void *)(a), (const void *)(b),	\
				 size, __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the specified value is not smaller than the minimum
 * and and not greater than the maximum.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if value is not in range.
 *
 * @param[in]  value  The value to check.
 *
 * @param[in]  minimum  The minimum value allowed.
 *
 * @param[in]  maximum  The maximum value allowed.
 */
void assert_in_range_msg(LargestIntegralType value,
			 LargestIntegralType minimum,
			 LargestIntegralType maximum);
#else
#define assert_in_range_msg(value, minimum, maximum)			\
	_assert_in_range(cast_to_largest_integral_type(value),		\
			 cast_to_largest_integral_type(minimum),	\
			 cast_to_largest_integral_type(maximum),	\
			 __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the specified value is smaller than the minimum or
 * greater than the maximum.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if value is in range.
 *
 * @param[in]  value  The value to check.
 *
 * @param[in]  minimum  The minimum value to compare.
 *
 * @param[in]  maximum  The maximum value to compare.
 */
void assert_not_in_range_msg(LargestIntegralType value,
			     LargestIntegralType minimum,
			     LargestIntegralType maximum);
#else
#define assert_not_in_range_msg(value, minimum, maximum)		\
	_assert_not_in_range(cast_to_largest_integral_type(value),	\
			     cast_to_largest_integral_type(minimum),	\
			     cast_to_largest_integral_type(maximum),	\
			     __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the specified value is within a set.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if value is not within a set.
 *
 * @param[in]  value  The value to look up
 *
 * @param[in]  values[]  The array to check for the value.
 *
 * @param[in]  count  The size of the values array.
 */
void assert_in_set_msg(LargestIntegralType
		       value, LargestIntegralType values[],
		       size_t count);
#else
#define assert_in_set_msg(value, values, number_of_values)		\
	_assert_in_set(value, values, number_of_values, __FILE__, __LINE__)
#endif

#ifdef DOXYGEN
/**
 * @brief Assert that the specified value is not within a set.
 *
 * The function prints an error message to standard error and terminates the
 * test by calling fail() if value is within a set.
 *
 * @param[in]  value  The value to look up
 *
 * @param[in]  values[]  The array to check for the value.
 *
 * @param[in]  count  The size of the values array.
 */
void assert_not_in_set_msg(LargestIntegralType value,
			   LargestIntegralType values[],
			   size_t count);
#else
#define assert_not_in_set_msg(value, values, number_of_values)		\
	_assert_not_in_set(value, values, number_of_values, __FILE__, __LINE__)
#endif

/** @} */


#endif /* !TEST_MAIN_H */
