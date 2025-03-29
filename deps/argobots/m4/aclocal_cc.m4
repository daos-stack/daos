dnl AC_PROG_CC_GNU
ifdef([AC_PROG_CC_GNU],,[AC_DEFUN([AC_PROG_CC_GNU],)])

dnl PAC_PROG_CC - reprioritize the C compiler search order
AC_DEFUN([PAC_PROG_CC],[
        dnl Many standard autoconf/automake/libtool macros, such as LT_INIT,
        dnl perform an AC_REQUIRE([AC_PROG_CC]).  If this macro (PAC_PROG_CC)
        dnl comes after LT_INIT (or similar) then the default compiler search
        dnl path will be used instead.  This AC_BEFORE macro ensures that a
        dnl warning will be emitted at autoconf-time (autogen.sh-time) to help
        dnl developers notice this case.
        AC_BEFORE([$0],[AC_PROG_CC])
	PAC_PUSH_FLAG([CFLAGS])
	AC_PROG_CC([icc pgcc xlc xlC pathcc gcc clang cc])
	PAC_POP_FLAG([CFLAGS])
])
dnl
dnl/*D
dnl PAC_C_CHECK_COMPILER_OPTION - Check that a compiler option is accepted
dnl without warning messages
dnl
dnl Synopsis:
dnl PAC_C_CHECK_COMPILER_OPTION(optionname,action-if-ok,action-if-fail)
dnl
dnl Output Effects:
dnl
dnl If no actions are specified, a working value is added to 'COPTIONS'
dnl
dnl Notes:
dnl This is now careful to check that the output is different, since 
dnl some compilers are noisy.
dnl 
dnl We are extra careful to prototype the functions in case compiler options
dnl that complain about poor code are in effect.
dnl
dnl Because this is a long script, we have ensured that you can pass a 
dnl variable containing the option name as the first argument.
dnl
dnl D*/
AC_DEFUN([PAC_C_CHECK_COMPILER_OPTION],[
AC_MSG_CHECKING([whether C compiler accepts option $1])
pac_opt="$1"
AC_LANG_PUSH([C])
CFLAGS_orig="$CFLAGS"
CFLAGS_opt="$pac_opt $CFLAGS"
pac_result="unknown"

AC_LANG_CONFTEST([AC_LANG_PROGRAM()])
CFLAGS="$CFLAGS_orig"
rm -f pac_test1.log
PAC_LINK_IFELSE_LOG([pac_test1.log], [], [
    CFLAGS="$CFLAGS_opt"
    rm -f pac_test2.log
    PAC_LINK_IFELSE_LOG([pac_test2.log], [], [
        PAC_RUNLOG_IFELSE([diff -b pac_test1.log pac_test2.log],
                          [pac_result=yes],[pac_result=no])
    ],[
        pac_result=no
    ])
], [
    pac_result=no
])
AC_MSG_RESULT([$pac_result])
dnl Delete the conftest created by AC_LANG_CONFTEST.
rm -f conftest.$ac_ext

# gcc 4.2.4 on 32-bit does not complain about the -Wno-type-limits option 
# even though it doesn't support it.  However, when another warning is 
# triggered, it gives an error that the option is not recognized.  So we 
# need to test with a conftest file that will generate warnings.
# 
# add an extra switch, pac_c_check_compiler_option_prototest, to
# disable this test just in case some new compiler does not like it.
#
# Linking with a program with an invalid prototype to ensure a compiler warning.

if test "$pac_result" = "yes" \
     -a "$pac_c_check_compiler_option_prototest" != "no" ; then
    AC_MSG_CHECKING([whether C compiler option $1 works with an invalid prototype program])
    AC_LINK_IFELSE([
        dnl We want a warning, but we don't want to inadvertently disable
        dnl special warnings like -Werror-implicit-function-declaration (e.g.,
        dnl in PAC_CC_STRICT) by compiling something that might actually be
        dnl treated as an error by the compiler.  So we try to elicit an
        dnl "unused variable" warning and/or an "uninitialized" warning with the
        dnl test program below.
        dnl
        dnl The old sanity program was:
        dnl   void main() {return 0;}
        dnl which clang (but not GCC) would treat as an *error*, invalidating
        dnl the test for any given parameter.
        AC_LANG_SOURCE([int main(int argc, char **argv){ int foo, bar = 0; foo += 1; return foo; }])
    ],[pac_result=yes],[pac_result=no])
    AC_MSG_RESULT([$pac_result])
fi
#
if test "$pac_result" = "yes" ; then
    AC_MSG_CHECKING([whether routines compiled with $pac_opt can be linked with ones compiled without $pac_opt])
    pac_result=unknown
    CFLAGS="$CFLAGS_orig"
    rm -f pac_test3.log
    PAC_COMPILE_IFELSE_LOG([pac_test3.log], [
        AC_LANG_SOURCE([
            int foo(void);
            int foo(void){return 0;}
        ])
    ],[
        PAC_RUNLOG([mv conftest.$OBJEXT pac_conftest.$OBJEXT])
        saved_LIBS="$LIBS"
        LIBS="pac_conftest.$OBJEXT $LIBS"

        rm -f pac_test4.log
        PAC_LINK_IFELSE_LOG([pac_test4.log], [AC_LANG_PROGRAM()], [
            CFLAGS="$CFLAGS_opt"
            rm -f pac_test5.log
            PAC_LINK_IFELSE_LOG([pac_test5.log], [AC_LANG_PROGRAM()], [
                PAC_RUNLOG_IFELSE([diff -b pac_test4.log pac_test5.log],
                                  [pac_result=yes], [pac_result=no])
            ],[
                pac_result=no
            ])
        ],[
            pac_result=no
        ])
        LIBS="$saved_LIBS"
        rm -f pac_conftest.$OBJEXT
    ],[
        pac_result=no
    ])
    AC_MSG_RESULT([$pac_result])
    rm -f pac_test3.log pac_test4.log pac_test5.log
fi
rm -f pac_test1.log pac_test2.log

dnl Restore CFLAGS before 2nd/3rd argument commands are executed,
dnl as 2nd/3rd argument command could be modifying CFLAGS.
CFLAGS="$CFLAGS_orig"
if test "$pac_result" = "yes" ; then
     ifelse([$2],[],[COPTIONS="$COPTIONS $1"],[$2])
else
     ifelse([$3],[],[:],[$3])
fi
AC_LANG_POP([C])
])
