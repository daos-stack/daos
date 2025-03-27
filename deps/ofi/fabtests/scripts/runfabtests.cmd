@echo off
setlocal EnableDelayedExpansion

if not "%selfWrapped%" == "%~0" (
	rem This is necessary so that we can use "exit' to terminate the
	rem batch file, and all subroutines, but not the original cmd.exe.
	set selfWrapped=%~0
	%ComSpec% /s /c ""%~0" %*"
	goto :EOF
)

set BIN_PATH=
set PROV=
set TEST_TYPE=quick
set VERBOSE=0
set SKIP_NEG=0
set SERVER=127.0.0.1
set S_INTERFACE=
set CLIENT=127.0.0.1
set C_INTERFACE=
set GOOD_ADDR=
set TIMEOUT_VAL=180
set STRICT_MODE=0
set OOB=0
set C_ARGS=
set S_ARGS=

set OPTIND=0
set PATH=%~dp0;%PATH%



set c_res=fabtests.c_res
set c_outp=fabtests.c_outp
set s_res=fabtests.s_res
set s_outp=fabtests.s_outp

set /a pass_count=0
set /a skip_count=0
set /a fail_count=0
set /a total_failures=0

set "spaces=                                                                                "

set unit_tests=^
	"getinfo_test -s SERVER_ADDR GOOD_ADDR"^
	"av_test -g GOOD_ADDR -n 1 -s SERVER_ADDR -e rdm"^
	"dom_test -n 2"^
	"eq_test"^
	"cq_test -L 4096"^
	"mr_test"^
	"cntr_test"

set neg_unit_tests=^
	"dgram g00n13s"^
	"rdm g00n13s"^
	"msg g00n13s"

set functional_tests=^
	"av_xfer -e rdm"^
	"cm_data"^
	"cq_data -e msg -o senddata"^
	"cq_data -e rdm -o senddata"^
	"cq_data -e msg -o writedata"^
	"cq_data -e rdm -o writedata"^
	"msg"^
	"msg_epoll"^
	"msg_sockets"^
	"poll -t queue"^
	"poll -t counter"^
	"rdm"^
	"rdm -U"^
	"rdm_tagged_peek"^
	"recv_cancel -e rdm -V"^
	"inject_test -A inject -v"^
	"inject_test -N -A inject -v"^
	"inject_test -A inj_complete -v"^
	"inject_test -N -A inj_complete -v"^
	"bw -e rdm -v -T 1"^
	"bw -e rdm -v -T 1 -U"^
	"bw -e msg -v -T 1"^
	"rdm_multi_client -C 10 -I 5"^
	"rdm_multi_client -C 10 -I 5 -U"

set short_tests=^
	"msg_pingpong -I 5"^
	"msg_pingpong -I 5 -v"^
	"msg_bw -I 5"^
	"msg_bw -I 5 -v"^
	"rma_bw -e msg -o write -I 5"^
	"rma_bw -e msg -o read -I 5"^
	"rma_bw -e msg -o writedata -I 5"^
	"rma_bw -e rdm -o write -I 5"^
	"rma_bw -e rdm -o write -I 5 -U"^
	"rma_bw -e rdm -o read -I 5"^
	"rma_bw -e rdm -o read -I 5 -U"^
	"rma_bw -e rdm -o writedata -I 5"^
	"rma_bw -e rdm -o writedata -I 5 -U"^
	"rdm_cntr_pingpong -I 5"^
	"multi_recv -e rdm -I 5"^
	"rdm_pingpong -I 5"^
	"rdm_pingpong -I 5 -U"^
	"rdm_pingpong -I 5 -v"^
	"rdm_pingpong -I 5 -v -U"^
	"rdm_tagged_pingpong -I 5"^
	"rdm_tagged_pingpong -I 5 -U"^
	"rdm_tagged_pingpong -I 5 -v"^
	"rdm_tagged_pingpong -I 5 -v -U"^
	"rdm_tagged_bw -I 5"^
	"rdm_tagged_bw -I 5 -U"^
	"rdm_tagged_bw -I 5 -v"^
	"rdm_tagged_bw -I 5 -v -U"

set standard_tests=^
	"msg_pingpong"^
	"msg_pingpong -v"^
	"msg_pingpong -k"^
	"msg_pingpong -k -v"^
	"msg_bw"^
	"msg_bw -v"^
	"rma_bw -e msg -o write"^
	"rma_bw -e msg -o read"^
	"rma_bw -e msg -o writedata"^
	"rma_bw -e rdm -o write"^
	"rma_bw -e rdm -o write -U"^
	"rma_bw -e rdm -o read"^
	"rma_bw -e rdm -o read -U"^
	"rma_bw -e rdm -o writedata"^
	"rma_bw -e rdm -o writedata -U"^
	"rdm_cntr_pingpong"^
	"multi_recv -e rdm"^
	"rdm_pingpong"^
	"rdm_pingpong -U"^
	"rdm_pingpong -v"^
	"rdm_pingpong -v -U"^
	"rdm_pingpong -k"^
	"rdm_pingpong -k -U"^
	"rdm_pingpong -k -v"^
	"rdm_pingpong -k -v -U"^
	"rdm_tagged_pingpong"^
	"rdm_tagged_pingpong -U"^
	"rdm_tagged_pingpong -v"^
	"rdm_tagged_pingpong -v -U"^
	"rdm_tagged_bw"^
	"rdm_tagged_bw -U"^
	"rdm_tagged_bw -v"^
	"rdm_tagged_bw -v -U"

set multinode_tests=^
	"multinode -x msg"^
	"multinode -x rma"


goto :global_main

:print_border
	echo # ------------------------------------------------------------------------------
	exit /b 0

:print_results
	set test_name=%~1
	set test_result=%~2
	set test_time=%~3
	set server_out_file=%~4
	set server_cmd=%~5
	set client_out_file=%~6
	set client_cmd=%~7

	if %VERBOSE% EQU 0 (
		rem print a simple, single-line format that is still valid YAML
		set left=%test_exe%:%spaces%
		set right=%spaces%%test_result%
		echo !left:~0,70!!right:~-10,10!
	) else (
		rem Print a more detailed YAML format that is not a superset of
		rem the non-verbose output.  See ofiwg/fabtests#259 for a
		rem rationale.
		set emit_stdout=0
		call :switch result %test_result% || (
:result-Pass
			if %VERBOSE% GEQ 3 set emit_stdout=1
			goto :EOF
:result-Notrun
			if %VERBOSE% GEQ 2 set emit_stdout=1
			goto :EOF
:result-Fail
			if %VERBOSE% GEQ 1 set emit_stdout=1
			goto :EOF
:result-
			echo Unknown result: %1 1>&2
			exit 1
		)

		echo - name:   %test_exe%
		echo   timestamp: %DATE% %TIME%
		echo   result: %test_result%
		echo   time:   %test_time%

		if !emit_stdout! EQU 1 (
			if not "%server_out_file%" == "" (
				if not "%server_cmd%" == "" (
					echo   server_cmd: %server_cmd%
				)
				echo   server_stdout:
				type %server_out_file%
				echo.
			)
			if not "%client_out_file%" == "" (
				if not "%client_cmd%" == "" (
					echo   client_cmd: %client_cmd%
				)
				echo   client_stdout:
				type %client_out_file%
				echo.
			)
		)
	)
	exit /b 0

:cleanup
	if exist %c_res% del /f %c_res%
	if exist %c_outp% del /f %c_outp%
	if exist %s_outp% del /f %s_outp%
	if exist %s_res% del /f %s_res%
	if not "%1" == "" (
		for /l %%i in (1,1,%1) do (
			if exist %c_res%%%i del /f %c_res%%%i
			if exist %c_outp%%%i del /f %c_outp%%%i
		)
	)
	exit /b

:compute_duration
	set start=%1
	set end=%2
	set /a s_m=%start:~-8,1%
	set /a s_m=%s_m% * 10 + %start:~-7,1%
	set /a s_s=%start:~-5,1%
	set /a s_s=%s_s% * 10 + %start:~-4,1%
	set /a e_m=%end:~-8,1%
	set /a e_m=%e_m% * 10 + %end:~-7,1%
	set /a e_s=%end:~-5,1%
	set /a e_s=%e_s% * 10 + %end:~-4,1%
	set /a min=(%e_m% - %s_m%)
	set /a sec=(%e_s% - %s_s%)
	set /a dur=%min% * 60 + %sec%
	if %dur% EQU 0 set /a dur=1
	set %3=%dur%
	exit /b 0

:wait_for_timeout
	set /a secs=0
:wait
	timeout /t 1 /nobreak >nul
	set /a secs+=1
	if %secs% GTR %TIMEOUT_VAL% (
		for /f "tokens=1" %%x in ("%test_exe%") do set image_name=%%x.exe
		taskkill /f /im !image_name! >nul
		set /a s_ret=124
		if %1 GEQ 2 set /a c_ret=124
		timeout /t 1 /nobreak >nul
		goto :wait_end
	)
	if not exist %s_res% goto :wait
	if %1 EQU 2 if not exist %c_res% goto :wait
	if %1 GTR 2 (
		set /a num_clients=%1 - 1
		for /l %%i in (1,1,!num_clients!) do (
		   if not exist %c_res%%%i goto :wait
		)
	)
	set /p s_ret=<%s_res%
	set /a s_ret=s_ret
	del %s_res%
	if %1 EQU 2 (
		set /p c_ret=<%c_res%
		set /a c_ret=c_ret
		del %c_res%
	)
	if %1 GTR 2 (
		set /a num_clients=%1 - 1
		for /l %%i in (1,1,!num_clients!) do (
			set /p ret=<%c_res%%%i
			set /a ret=ret
			del %c_res%%%i
			if !ret! NEQ 0 set c_ret=!ret!
		)
	)
:wait_end
	exit /b 0

:unit_test
	set test=%~1
	set is_neg=%2
	set s_ret=
	set start_time=
	set end_time=
	set test_time=

	if %OOB% EQU 1 (
		set interface=%GOOD_ADDR%
	) else (
		set interface=%S_INTERFACE%
	)
	set test_exe=%test% -p %PROV%
	set test_exe=%test_exe:GOOD_ADDR=!GOOD_ADDR!%
	set test_exe=%test_exe:SERVER_ADDR=!interface!%

	rem getinfo_test will fail without having set FI_PROVIDER
	rem due to how putenv works on Windows.
	rem https://github.com/ofiwg/libfabric/issues/7565
	if "%test:~0,12%" == "getinfo_test" set FI_PROVIDER=%PROV%

	set start_time=%TIME%

	set cmd=%BIN_PATH%%test_exe%
	start /b run_with_output.cmd "%cmd%" %s_outp% %s_res% >nul 2>nul
	call :wait_for_timeout 1

	set end_time=%TIME%
	call :compute_duration %start_time% %end_time% test_time

	if "%test:~0,12%" == "getinfo_test" set FI_PROVIDER=

	if "%is_neg%%s_ret%" == "1120" (
		rem Negative test passed.
		set s_ret=0
	)
	if "%STRICT_MODE%%s_ret%" == "0120" (
		call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%cmd%"
		set /a skip_count+=1
	) else (
		if "%STRICT_MODE%%s_ret%" == "040" (
			call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%cmd%"
			set /a skip_count+=1
		) else (
			if %s_ret% NEQ 0 (
				call :print_results "%test_exe%" "Fail" "%test_time%" "%s_outp%" "%cmd%"
				if %s_ret% EQU 124 (
					rem timed out
					call :cleanup
				)
				set /a fail_count+=1
			) else (
				call :print_results "%test_exe%" "Pass" "%test_time%" "%s_outp%" "%cmd%"
				set /a pass_count+=1
			)
		)
	)
	exit /b 0

:cs_test
	set test=%~1
	set s_ret=0
	set c_ret=0
	set test_exe=%test% -p %PROV%
	set start_time=
	set end_time=
	set test_time=

	set start_time=%TIME%

	if %OOB% EQU 1 (
		set s_arg=-E
	) else (
		set s_arg=-s %S_INTERFACE%
	)
	set s_cmd=%BIN_PATH%%test_exe% %S_ARGS% %s_arg%
	start /b run_with_output.cmd "%s_cmd%" %s_outp% %s_res% >nul 2>nul
	timeout /t 1 /nobreak >nul

	if %OOB% EQU 1 (
		set c_arg=-E %S_INTERFACE%
	) else (
		set c_arg=-s %C_INTERFACE% %S_INTERFACE%
	)
	set c_cmd=%BIN_PATH%%test_exe% %C_ARGS% %c_arg%
	start /b run_with_output.cmd "%c_cmd%" %c_outp% %c_res% >nul 2>nul
	call :wait_for_timeout 2

	set end_time=%TIME%
	call :compute_duration %start_time% %end_time% test_time

	if "%STRICT_MODE%%s_ret%%c_ret%" == "0120120" (
		call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%s_cmd%" "%c_outp%" "%c_cmd%"
		set /a skip_count+=1
	) else (
		if "%STRICT_MODE%%s_ret%%c_ret%" == "04040" (
			call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%s_cmd%" "%c_outp%" "%c_cmd%"
			set /a skip_count+=1
		) else (
			if %s_ret% NEQ 0 (
				call :print_results "%test_exe%" "Fail" "%test_time%" "%s_outp%" "%s_cmd%" "%c_outp%" "%c_cmd%"
				if %s_ret% EQU 124 (
					rem timed out
					call :cleanup
				) else (
					if %c_ret% EQU 124 (
						rem timed out
						call :cleanup
					)
				)
				set /a fail_count+=1
			) else (
				if %c_ret% NEQ 0 (
					call :print_results "%test_exe%" "Fail" "%test_time%" "%s_outp%" "%s_cmd%" "%c_outp%" "%c_cmd%"
					if %s_ret% EQU 124 (
						rem timed out
						call :cleanup
					) else (
						if %c_ret% EQU 124 (
							rem timed out
							call :cleanup
						)
					)
					set /a fail_count+=1
				) else (
					call :print_results "%test_exe%" "Pass" "%test_time%" "%s_outp%" "%s_cmd%" "%c_outp%" "%c_cmd%"
					set /a pass_count+=1
				)
			)
		)
	)
	exit /b 0

:multinode_test
	set test=%~1
	set s_ret=0
	set c_ret=0
	set /a num_procs=%2
	set /a num_clients=%num_procs% - 1
	set test_exe=%test% -n %num_procs% -p "%PROV%"
	set c_out=
	set start_time=
	set end_time=
	set test_time=

	set start_time=%TIME%

	set s_cmd=%BIN_PATH%%test_exe% %S_ARGS% -s %S_INTERFACE%
	start /b run_with_output.cmd "%s_cmd%" %s_outp% %s_res% >nul 2>nul
	timeout /t 1 /nobreak >nul

	for /l %%i in (1,1,%num_clients%) do (
		set res=%c_res%%%i
		set out=%c_outp%%%i
		set c_cmd=%BIN_PATH%%test_exe% %S_ARGS% -s %S_INTERFACE%
		start /b run_with_output.cmd "!c_cmd!" !out! !res! >nul 2>nul
	)

	call :wait_for_timeout %num_procs%
	echo server finished

	set end_time=%TIME%
	call :compute_duration %start_time% %end_time% test_time

	set /a pe=1
	if "%STRICT_MODE%%s_ret%%c_ret%" == "0120120" (
		call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%s_cmd%" "" "%c_cmd%"
		for /l %%i in (1,1,%num_clients%) do (
			echo   client_stdout !pe!:
			type !c_outp!%%i
			echo.
			set /a pe+=1
		)
		set /a skip_count+=1
	) else (
		if "%STRICT_MODE%%s_ret%%c_ret%" == "04040" (
			call :print_results "%test_exe%" "Notrun" "%test_time%" "%s_outp%" "%s_cmd%" "" "%c_cmd%"
			for /l %%i in (1,1,%num_clients%) do (
				echo   client_stdout !pe!:
				type !c_outp!%%i
				echo.
				set /a pe+=1
			)
			set /a skip_count+=1
		) else (
			if %s_ret% NEQ 0 (
				call :print_results "%test_exe%" "Fail" "%test_time%" "%s_outp%" "%s_cmd%" "" "%c_cmd%"
				for /l %%i in (1,1,%num_clients%) do (
					echo   client_stdout !pe!:
					type !c_outp!%%i
					echo.
					set /a pe+=1
				)
				if %s_ret% EQU 124 (
					rem timed out
					call :cleanup
				) else (
					if %c_ret% EQU 124 (
						rem timed out
						call :cleanup
					)
				)
				set /a fail_count+=1
			) else (
				if %c_ret% NEQ 0 (
					call :print_results "%test_exe%" "Fail" "%test_time%" "%s_outp%" "%s_cmd%" "" "%c_cmd%"
					for /l %%i in (1,1,%num_clients%) do (
						echo   client_stdout !pe!:
						type !c_outp!%%i
						echo.
						set /a pe+=1
					)
					if %s_ret% EQU 124 (
						rem timed out
						call :cleanup
					) else (
						if %c_ret% EQU 124 (
							rem timed out
							call :cleanup
						)
					)
					set /a fail_count+=1
				) else (
					call :print_results "%test_exe%" "Pass" "%test_time%" "%s_outp%" "%s_cmd%" "" "%c_cmd%"
					for /l %%i in (1,1,%num_clients%) do (
						echo   client_stdout !pe!:
						type !c_outp!%%i
						echo.
						set /a pe+=1
					)
					set /a pass_count+=1
				)
			)
		)
	)
	call :cleanup %num_clients%
	exit /b 0

:switch
	goto :%1-%2 2>nul || (
		type nul>nul
		call :%1- %2
	)
	exit /b

:main
	set skip_count=0
	set pass_count=0
	set fail_count=0

	set tests=
	if /i "%~1" == "quick" set tests=unit,functional,short
	if /i "%~1" == "all" set tests=unit,functional,standard,multinode
	if /i "%tests%" == "" set tests=%~1
	set tests=%tests:,= %

	if %VERBOSE% == 0 (
		echo # Test                                                                    Result
		call :print_border
	)

	for %%s in (%tests%) do (
		call :switch set %%s || (
:set-unit
			for %%t in (%unit_tests%) do (
				call :unit_test %%t 0
			)
			if "%SKIP_NEG%" == "0" (
				for %%t in (%neg_unit_tests%) do (
					call :unit_test %%t 1
				)
			)
			goto :EOF
:set-functional
			for %%t in (%functional_tests%) do (
				call :cs_test %%t
			)
			goto :EOF
:set-short
			for %%t in (%short_tests%) do (
				call :cs_test %%t
			)
			goto :EOF
:set-standard
			for %%t in (%standard_tests%) do (
				call :cs_test %%t
			)
			goto :EOF
:set-multinode
			for %%t in (%multinode_tests%) do (
				call :multinode_test %%t 3
			)
			goto :EOF
:set-
			echo Unknown test set: %1 1>&2
			exit 1
		)
	)

	set /a total= %pass_count% + %fail_count%

	call :print_border

	set left=Total Pass%spaces%
	set right=%spaces%%pass_count%
	echo # %left:~0,50%%right:~-10,10%
	set left=Total Notrun/Excluded%spaces%
	set right=%spaces%%skip_count%
	echo # %left:~0,50%%right:~-10,10%
	set left=Total Fail%spaces%
	set right=%spaces%%fail_count%
	echo # %left:~0,50%%right:~-10,10%

	if %total% GTR 0 (
		set /a pass_pct="(%pass_count% * 100) / %total%"
		set left=Percentage of Pass%spaces%
		set right=%spaces%!pass_pct!
		echo # !left:~0,50!!right:~-10,10!
	)

	call :print_border

	call :cleanup
	set /a total_failures+=%fail_count%
	exit /b

:usage
	echo.Usage: 1>&2
	echo.  runfabtests.cmd [OPTIONS] [provider] [host] [client] 1>&2
	echo. 1>&2
	echo.Run fabtests using provider between host and client (default 1>&2
	echo.'sockets' provider in loopback-mode).  Report pass/fail/notrun status. 1>&2
	echo. 1>&2
	echo.Options... 1>&2
	echo.  -g       good IP address from [host]'s perspective (default %GOOD_ADDR%) 1>&2
	echo.  -v       print output of failing 1>&2
	echo.  -v -v    print output of failing/notrun 1>&2
	echo.  -v -v -v print output of failing/notrun/passing 1>&2
	echo.  -t       test set(s): all,quick,unit,functional,standard,short,complex (default quick) 1>&2
	echo.  -N       skip negative unit tests 1>&2
	echo.  -p       path to test bins (default PATH) 1>&2
	echo.  -c       client interface 1>&2
	echo.  -s       server/host interface 1>&2
	echo.  -T       timeout value in seconds 1>&2
	echo.  -S       Strict mode: -FI_ENODATA, -FI_ENOSYS errors would be treated as failures instead of skipped/notrun 1>&2
	echo.  -C       Additional client test arguments: Parameters to pass to client fabtests 1>&2
	echo.  -L       Additional server test arguments: Parameters to pass to server fabtests 1>&2
	echo.  -b       enable out-of-band address exchange over the default port 1>&2
	exit 1

:getopts
	if "%1" == "-t" (
		set TEST_TYPE=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-v" (
		set /a VERBOSE+=1
		goto nextopt
	)
	if "%1" == "-p" (
		set BIN_PATH=%~2\
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-g" (
		set GOOD_ADDR=%~2\
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-c" (
		set C_INTERFACE=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-s" (
		set S_INTERFACE=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-T" (
		set /a TIMEOUT_VAL=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-N" (
		set /a SKIP_NEG+=1
		goto nextopt
	)
	if "%1" == "-S" (
		set /a STRICT_MODE+=1
		goto nextopt
	)
	if "%1" == "-b" (
		set /a OOB+=1
		goto nextopt
	)
	if "%1" == "-C" (
		set C_ARGS=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-L" (
		set S_ARGS=%~2
		shift
		set /a OPTIND+=1
		goto nextopt
	)
	if "%1" == "-h" (
		goto :usage
	)
:nextopt
	shift & set /a OPTIND+=1
	set x=%1
	if /i "%x:~0,1%" == "-" goto getopts
	exit /b 0

:global_main
	call :getopts %*
	for /L %%a in (1,1,!OPTIND!) do shift

	if not "%1" == "" (
		set PROV=%1
		shift
	)
	if not "%1" == "" (
		set SERVER=%1
		shift
	)
	if not "%1" == "" (
		set CLIENT=%1
		shift
	)
	if not "%1" == "" goto :usage

	if "%C_INTERFACE%" == "" set C_INTERFACE=%CLIENT%
	if "%S_INTERFACE%" == "" set S_INTERFACE=%SERVER%
	if "%GOOD_ADDR%" == "" set GOOD_ADDR=%S_INTERFACE%

	if "%PROV%" == "" (
		set PROV=sockets
		call :main "%TEST_TYPE%"
	) else (
		call :main "%TEST_TYPE%"
	)

	exit /b %total_failures%
