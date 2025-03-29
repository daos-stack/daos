dnl Nesting safe macros for saving variables
dnl Usage: PAC_PUSH_FLAG(CFLAGS)
AC_DEFUN([PAC_PUSH_FLAG],[
	if test -z "${pac_save_$1_nesting}" ; then
	   pac_save_$1_nesting=0
	fi
	eval pac_save_$1_${pac_save_$1_nesting}='"$$1"'
	pac_save_$1_nesting=`expr ${pac_save_$1_nesting} + 1`
])

dnl Usage: PAC_POP_FLAG(CFLAGS)
AC_DEFUN([PAC_POP_FLAG],[
	pac_save_$1_nesting=`expr ${pac_save_$1_nesting} - 1`
	eval $1="\$pac_save_$1_${pac_save_$1_nesting}"
	eval pac_save_$1_${pac_save_$1_nesting}=""
])

dnl Usage: PAC_APPEND_FLAG([-02], [CFLAGS])
dnl appends the given argument to the specified shell variable unless the
dnl argument is already present in the variable
AC_DEFUN([PAC_APPEND_FLAG],[
	AC_REQUIRE([AC_PROG_FGREP])
	AS_IF(
		[echo "$$2" | $FGREP -e '$1' >/dev/null 2>&1],
		[echo "$2(='$$2') contains '$1', not appending" >&AS_MESSAGE_LOG_FD],
		[echo "$2(='$$2') does not contain '$1', appending" >&AS_MESSAGE_LOG_FD
		$2="$$2 $1"]
	)
])

dnl Usage: PAC_PREPEND_FLAG([-lpthread], [LIBS])
dnl Prepends the given argument to the specified shell variable unless the
dnl argument is already present in the variable.
dnl
dnl This is typically used for LIBS and similar variables because libraries
dnl should be added in reverse order.
AC_DEFUN([PAC_PREPEND_FLAG],[
        AC_REQUIRE([AC_PROG_FGREP])
        AS_IF(
                [echo "$$2" | $FGREP -e '$1' >/dev/null 2>&1],
                [echo "$2(='$$2') contains '$1', not prepending" >&AS_MESSAGE_LOG_FD],
                [echo "$2(='$$2') does not contain '$1', prepending" >&AS_MESSAGE_LOG_FD
                $2="$1 $$2"]
        )
])
