# EFA unit tests

## How to run

To run efa unit tests, you will need to have cmocka installed.
* [Cmocka Mirror](https://cmocka.org/files/)
* [Install Instructions](https://gitlab.com/cmocka/cmocka/-/blob/master/INSTALL.md)

You will need to configure libfabric with `--enable-efa-unit-test=<path_to_cmocka_install>`.

An example build and run command would look like:

```bash
./autogen.sh && ./configure --enable-efa-unit-test=/home/ec2-user/cmocka/install && make check;
```

If `make check` fails, then directly executing the test executable also works
```
./prov/efa/test/efa_unit_test
```

## File Structure
* `efa_unit_tests.*`: They are the entry point to test runs. Declare unit tests in the header file, and add them to the `main` function.
* `efa_unit_test_mocks.*`: As the name suggests, define function mocks here.
* `efa_unit_test_commont.c`: This file contains utilities shared by different tests.
* `efa_unit_test_{component}.c`: Implement unit tests in the corresponding file.

## What Should be Tested
1. We are biased toward testing public, stable functions, e.g. we prefer testing `fi_*` to `efa_*` - the former more closely resembles real user behavior.
1. We make a conscious trade-off to test larger rather than smaller units. A large test unit consists of more state variables, and testing it gives us more confidence in the overall system. 
1. We avoid testing `static` functions that are implementation details and subject to frequent changes. More importantly, `static` function tests have to be written in the same source files and difficult to manage in a central place.
1. We are biased toward testing edge cases over "happy cases", especially if the code path under test cannot be covered by integration tests.

## How to write
1. Decide the component under test, i.e. determine which `efa_unit_test_{component}.c` the test belongs to.
1. Write the test in the above test file.
1. Declare the test in `efa_unit_tests.h`.
1. Create or find an existing cmocka test group in `efa_unit_tests.c`, and the test to the group.
1. If you need to use `struct efa_resource`, then accept `struct efa_resource **resource` as as input to the test function. The framework will automatically create the resources before the test and destroy them after the test.

## Mocking
* To mock a function, you need to make sure that the function is declared in a header file, and defined in a different file from where it is called. You will need to add `-Wl,--wrap=<function to mock>`
to the Makefile.include as part of efatest_LIBS. Then, after declaring the function,
you can replace it with `__wrap_<function to mock>`. If you need to use the original
function, you can use `__real_<function to mock>` after declaring it.
  * Recreate the funciton signature with `__wrap_<function>(the_params)`, and the function to the **Makefile.include** `prov_efa_test_efa_unit_test_LDFLAGS` list
  * Check all parameters with `check_expected()`. This allows test code to optionally check the parameters of the mocked function with the family of `expect_value()` functions.
  *  Mock the function return value using `will_return(__wrap_xxx, mocked_val)`. Inside the mocked function, access `mocked_val` using `mock()`. This gives the test code control of the return value of the mocked function. The `will_return()` function creates a stack for each mocked function and returns the top of the stack first.
  * Because cmocka does mocking via linker, calls to a function will be wrapped everywhere - you might break existing tests by introducing a new mock! To avoid this, there is a trick to automatically reset the mock after each test with the help of a global variable `g_efa_unit_test_mocks` and teardown hook `efa_unit_test_mocks_teardown` - check it out.
  * Keep mocks in `efa_unit_test_mocks.*`
* To mock a function pointer, there is no need for the linker magic. Simply implement the mocked function in `efa_unit_test_mocks.*`, and pass a pointer to the function where mocking is needed.

## Manipulating Mock Behavior

You can use the cmocka API to change the behavior of your mock function.

The will_return class of functions will place values onto a stack that can
be popped within the function with the mock function. You can use this to
manipulate the function behavior. For example, if you pop the value 'true'
using
```c
will_return(mock_function, true)
```

and add a check in your mock function like so:

```c
int use_real_function = mock_type(bool);
if (use_real_function)
	return __real_mock_function(params);
```

you can use the real function whenever you wish to instead of your custom
behavior.

See: https://api.cmocka.org/group__cmocka__mock.html for more details
