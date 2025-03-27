@echo off
REM // Copyright (c) 2020-2021, Intel Corporation
REM // 
REM // Redistribution and use in source and binary forms, with or without
REM // modification, are permitted provided that the following conditions are met:
REM // 
REM //     * Redistributions of source code must retain the above copyright notice,
REM //       this list of conditions and the following disclaimer.
REM //     * Redistributions in binary form must reproduce the above copyright
REM //       notice, this list of conditions and the following disclaimer in the
REM //       documentation and/or other materials provided with the distribution.
REM //     * Neither the name of Intel Corporation nor the names of its contributors
REM //       may be used to endorse or promote products derived from this software
REM //       without specific prior written permission.
REM // 
REM // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
REM // AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
REM // IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
REM // DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
REM // FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
REM // DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
REM // SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
REM // CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
REM // OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
REM // OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

REM // 
REM // Script to produce C/C++ dependencies based using CL compiler
REM // 
REM // Using: deps.bat arg1 arg2 arg3
REM // 
REM // arg1 - C/C++ file name to produce dependecies for
REM // arg2 - object file corresponding to the C/C++ file name
REM // arg3 - include switches for the compiler
REM // 
REM // Notes:
REM //   - 'cl' command is hardcoded below
REM //

@echo %1 : \
@for /f "tokens=1,2,3,*" %%g in ('cl /Zs /showIncludes /nologo %~3 /c %1') do @if not "%%j"=="" echo   "%%j" \
@echo.
@echo %2 : %1
