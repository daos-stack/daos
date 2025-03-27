#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import tests_config
import re
import os
import sys
import textwrap


class TestGenerator(object):
    main_UT_dir = ""
    main_tested_dir = ""
    tested_file_path = ""
    tested_function_name = ""

    def __init__(self, main_UT_dir, main_tested_dir, file_path, func_name):
        self.set_main_UT_dir(main_UT_dir)
        self.set_main_tested_dir(main_tested_dir)
        self.set_tested_file_path(file_path)
        self.tested_function_name = func_name

    def create_empty_test_file(self):
        dst_dir = os.path.dirname(self.get_tested_file_path()[::-1])[::-1]

        self.create_dir_if_not_exist(self.get_main_UT_dir() + dst_dir)
        test_file_name = os.path.basename(self.get_tested_file_path())

        dst_path = self.get_main_UT_dir() + dst_dir + "/" + test_file_name

        no_str = ""
        no = 0
        while True:
            if not os.path.isfile("{0}{1}.{2}".format(dst_path.rsplit(".", 1)[0], no_str,
                                                      dst_path.rsplit(".", 1)[1])):
                break
            no += 1
            no_str = str(no)

        dst_path = dst_path.rsplit(".", 1)[0] + no_str + "." + dst_path.rsplit(".", 1)[1]
        buf = self.get_markups()
        buf += "#undef static\n\n"
        buf += "#undef inline\n\n"
        buf += self.get_UT_includes()
        buf += self.get_includes(self.get_main_tested_dir() + self.get_tested_file_path())
        buf += self.get_autowrap_file_include(dst_path)
        buf += self.get_empty_test_function()
        buf += self.get_test_main()

        with open(dst_path, "w") as f:
            f.writelines(buf)

        print(f"{dst_path} generated successfully!")

    def get_markups(self):
        ret = "/*\n"
        ret += " * <tested_file_path>" + self.get_tested_file_path() + "</tested_file_path>\n"
        ret += " * <tested_function>" + self.get_tested_function_name() + "</tested_function>\n"
        ret += " * <functions_to_leave>\n"
        ret += " *\tINSERT HERE LIST OF FUNCTIONS YOU WANT TO LEAVE\n"
        ret += " *\tONE FUNCTION PER LINE\n"
        ret += " * </functions_to_leave>\n"
        ret += " */\n\n"

        return ret

    def create_dir_if_not_exist(self, path):
        if not os.path.isdir(path):
            try:
                os.makedirs(path)
            except Exception:
                pass
            return True
        return None

    def get_UT_includes(self):
        ret = '''
            #include <stdarg.h>
            #include <stddef.h>
            #include <setjmp.h>
            #include <cmocka.h>
            #include "print_desc.h"\n\n'''

        return textwrap.dedent(ret)

    def get_autowrap_file_include(self, test_file_path):
        autowrap_file = test_file_path.rsplit(".", 1)[0]
        autowrap_file = autowrap_file.replace(self.main_UT_dir, "")
        autowrap_file += "_generated_wraps.c"
        return "#include \"" + autowrap_file + "\"\n\n"

    def get_includes(self, abs_path_to_tested_file):
        with open(abs_path_to_tested_file, "r") as f:
            code = f.readlines()

        ret = [line for line in code if re.search(r'#include', line)]

        return "".join(ret) + "\n"

    def get_empty_test_function(self):
        ret = "static void " + self.get_tested_function_name() + "_test01(void **state)\n"
        ret += "{\n"
        ret += "\tprint_test_description(\"Put test description here\\n\");\n"
        ret += "\tassert_int_equal(1,1);\n"
        ret += "}\n\n"

        return ret

    def get_test_main(self):
        ret = "int main(void)\n"
        ret += "{\n"
        ret += "\tconst struct CMUnitTest tests[] = {\n"
        ret += "\t\tcmocka_unit_test(" + self.get_tested_function_name() + "_test01)\n"
        ret += "\t};\n\n"
        ret += "\tprint_message(\"Unit test for " + self.get_tested_function_name() + "\\n\");\n\n"
        ret += "\treturn cmocka_run_group_tests(tests, NULL, NULL);\n"
        ret += "}"

        return ret

    def set_tested_file_path(self, path):
        call_dir = os.getcwd() + os.sep
        p = os.path.normpath(call_dir + path)

        if os.path.isfile(p):
            self.tested_file_path = p.split(self.get_main_tested_dir(), 1)[1]
            return
        elif os.path.isfile(self.get_main_tested_dir() + path):
            self.tested_file_path = path
            return

        print(f"{os.path.join(self.get_main_tested_dir(), path)}")
        print("Given path not exists!")
        exit(1)

    def set_main_UT_dir(self, path):
        p = os.path.dirname(os.path.realpath(__file__)) + os.sep + path
        p = os.path.normpath(os.path.dirname(p)) + os.sep
        self.main_UT_dir = p

    def get_main_UT_dir(self):
        return self.main_UT_dir

    def set_main_tested_dir(self, path):
        p = os.path.dirname(os.path.realpath(__file__)) + os.sep + path
        p = os.path.normpath(os.path.dirname(p)) + os.sep
        self.main_tested_dir = p

    def get_main_tested_dir(self):
        return self.main_tested_dir

    def get_tested_file_path(self):
        return self.tested_file_path

    def get_tested_function_name(self):
        return self.tested_function_name


def __main__():
    if len(sys.argv) < 3:
        print("No path to tested file or tested function name given !")
        sys.exit(1)

    tested_file_path = sys.argv[1]
    tested_function_name = sys.argv[2]

    generator = TestGenerator(tests_config.MAIN_DIRECTORY_OF_UNIT_TESTS,
                              tests_config.MAIN_DIRECTORY_OF_TESTED_PROJECT,
                              tested_file_path, tested_function_name)

    generator.create_empty_test_file()


if __name__ == "__main__":
    __main__()
