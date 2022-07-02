"""Module for setting up protoc in scons"""
# Copyright 2018-2022 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

import os
import SCons.Builder
# pylint: disable=too-few-public-methods,missing-class-docstring


class ToolProtocWarning(SCons.Warnings.Warning):
    pass


class ProtocCompilerNotFound(ToolProtocWarning):
    pass


class GoProtocCompilerNotFound(ToolProtocWarning):
    pass


class PythonGRPCCompilerNotFound(ToolProtocWarning):
    pass


SCons.Warnings.enableWarningClass(ToolProtocWarning)


def _detect(env):
    """ Try to detect the various protoc components """
    protoc_found = False
    protoc_gen_go_found = False
    grpc_tools_found = False

    # Make sure we have protoc installed and in the path
    if "PROTOC" in env:
        protoc_found = True
    else:
        # Check to see if we have a compiled protoc
        protoc = os.path.join(env['PROTOBUF_PREFIX'], 'bin', 'protoc')
        if os.path.isfile(protoc) and os.access(protoc, os.X_OK):
            env['PROTOC'] = protoc
            protoc_found = True
        else:
            protoc = env.WhereIs('protoc')
            if protoc:
                env['PROTOC'] = protoc
                protoc_found = True

    # Make sure we have protoc-gen-go installed and in the path
    # (Should mean GOPATH/bin is in the path)
    if "PROTOC-GEN-GO" in env:
        protoc_gen_go_found = True
    else:
        protoc_gen_go = env.WhereIs('protoc-gen-go')
        if protoc_gen_go:
            protoc_gen_go_found = True

    try:
        # pylint: disable=unused-import,import-outside-toplevel
        import grpc_tools.protoc  # noqa: F401
        grpc_tools_found = True
    except ImportError:
        grpc_tools_found = False

    if protoc_found and protoc_gen_go_found and grpc_tools_found:
        return True
    if not protoc_found:
        raise SCons.Errors.StopError(ProtocCompilerNotFound,
                                     "Could not detect protoc compiler")
    if not protoc_gen_go_found:
        raise SCons.Errors.StopError(GoProtocCompilerNotFound,
                                     "Could not detect protoc-gen-go")
    if not grpc_tools_found:
        raise SCons.Errors.StopError(PythonGRPCCompilerNotFound,
                                     "grpc_tools.protoc python module is not installed")
    return None


# pylint: disable-next=missing-function-docstring
def run_python(_source, _target, env, _for_signature):
    actions = []
    mkdir_str = "mkdir -p " + env.subst('$GTARGET_DIR')
    actions.append(mkdir_str)
    actions.append('$PYTHON_COM')
    return actions


_grpc_python_builder = SCons.Builder.Builder(
    generator=run_python,
    suffix='$PYTHON_SUFFIX',
    src_suffix='$PROTO_SUFFIX',
    single_source=1
)


# pylint: disable-next=missing-function-docstring
def run_go(_source, _target, env, _for_signature):
    actions = []
    mkdir_str = "mkdir -p " + env.subst('$GTARGET_DIR')
    actions.append(mkdir_str)
    actions.append('$GO_COM')
    return actions


_grpc_go_builder = SCons.Builder.Builder(
    generator=run_go,
    suffix='$GO_SUFFIX',
    src_suffix='$PROTO_SUFFIX',
    single_source=1
)


def generate(env, **_kwargs):
    """Callback to setup module"""
    _detect(env)

    env.SetDefault(
        PYTHON_SUFFIX='_pb2_grpc.py',
        GO_SUFFIX='.pb.go',
        PROTO_SUFFIX='.proto',
        PYTHON_COM='python -m grpc_tools.protoc -I$PROTO_INCLUDES '
        + '--python_out=$GTARGET_DIR --grpc_python_out=$GTARGET_DIR $SOURCE',
        PYTHON_COMSTR='',
        GO_COM='$PROTOC -I$PROTO_INCLUDES $SOURCE --go_out=plugins=grpc:$GTARGET_DIR',
        GO_COMSTR=''
    )
    env['BUILDERS']['GRPCPython'] = _grpc_python_builder
    env['BUILDERS']['GRPCGo'] = _grpc_go_builder


def exists(env):
    """Callback to setup module"""
    return _detect(env)
