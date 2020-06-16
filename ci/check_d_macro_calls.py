#!/usr/bin/env python3

import subprocess

CMD=['git', 'grep', '-n', '-B1', '-A1', '-e', 'D_FREE', '-e', 'D_FREE_PTR']

"""
Script to check for D_FREE usage in the codebase.

The D_FREE() macro both allows for NULL to be passed and sets the variable
to NULL after the free, so do not use conditionals before calling this,
and do not do an assignment to NULL afterwards.

We used to use an additional D_FREE_PTR() macro as well, so remove this
on any code being patched.

This script should be run directly, and it will output in patch format so can
be applied using the following:

$ ./check_d_free_calls.py | patch -p1

"""

class code_line():

    def __init__(self, raw):
        try:
            (fn, ln, txt) = raw.split('-', 2)
        except ValueError:
            (fn, ln, txt) = raw.split(':', 2)

        # The filename and line number.
        self.filename = fn
        self.lineno = ln
        # Line of file, as it exists in the file.
        self.text = txt

        # Work out self.code, the meaningful code on this line.
        short = txt.strip()
        self.code = short
        sc = short.find('/*')
        if sc != -1:
            ec = short.find('*/')
            self.code = short[:sc] + short[ec+2:]
        # Strip out any sections from multi-line comments.
        if short.startswith('*'):
            self.code = ''

        # Set self.free_var to the name of anything being freed, or Null.
        if self.code.startswith('D_FREE'):
            _, val = short.split('(',1)
            self.free_var = val[:-2]
        else:
            self.free_var = None

        self.conditional = False
        self.conditional_brace = False
        self.close_brace = False
        self.cond_str = ''

        if self.code.startswith('if ('):
            self.conditional = True
            if self.code.endswith('{'):
                self.conditional_brace = True
            self.cond_str = self.code[4:].rstrip(' ){')

        if self.code.startswith('for ('):
            if self.code.endswith('{'):
                self.conditional_brace = True

        if self.code == '}':
            self.close_brace = True

        # Set to True if line is to be changed in patch
        self.changed = False
        # Set to False if line is to be removed.
        self.include = True
        # New text, if self.changed is True
        self.new_text = None

    def _set_text(self, text):
        # Mark a line as requiring change.
        self.new_text = text
        self.code = self.new_text.strip()
        self.changed = True

    def _get_text(self):
        # Get the text for a line, which may already be marked
        # as requiring change
        if self.new_text:
            return self.new_text
        return self.text

    def is_cond_on_var(self, var):
        # Check if a line is a conditional on a variable
        # being non-zero.
        if not self.conditional:
            return False
        if var == self.cond_str:
            return True
        if '{} != NULL'.format(var) == self.cond_str:
            return True
        return False

    def is_assign_null(self, var):
        # Check is a line is assigning a variable to NULL
        return self.code == '{} = NULL;'.format(var)

    def _is_cond_part_on_var(self, var):
        for ending in ['&& {}'.format(var),
                       '&& {} != NULL'.format(var)]:
            if self.cond_str.endswith(ending):
                strip_len = len(ending)
                strip_len += 1
                if self.conditional_brace:
                    strip_len += 2
                old_text = self._get_text()
                new_cond = old_text[:-strip_len].rstrip() + ')'
                if new_cond.endswith('))') and '&&' not in new_cond:
                    new_cond = new_cond[:-1]
                    new_cond = new_cond.replace('((', '(', 1)
                if self.conditional_brace:
                    new_cond += ' {'
                return (True, new_cond)
        return (False, None)

    def is_cond_part_on_var(self, var):
        # Check if a conditional line includes a conditional on a variable
        # in addition to another check.
        (ret, _) = self._is_cond_part_on_var(var)
        return ret

    def shorten_cond(self, var):
        # If is_cond_part_on_var() returns true then remove the part of
        # the conditional that is dependent on the variable.
        (ret, new_code) = self._is_cond_part_on_var(var)
        assert ret
        self.code = new_code
        self._set_text(new_code)

    def remove_brace(self):
        # Strip the open brace from the end of a conditional
        assert self.conditional_brace
        self._set_text(self.text.rstrip('{ '))
        self.conditional_brace = False

    def drop_line(self):
        # Mark a line as not for inclusion.
        self.include = False

    def shift_left(self):
        # Move the code on a line left one tabstop.
        self._set_text(self.text[1:])

    def remove_free_ptr(self):
        # Replace use of D_FREE_PTR
        if 'D_FREE_PTR' in self.code:
            self._set_text(self._get_text().replace('D_FREE_PTR', 'D_FREE'))

prev_file = None
def show_patch(lines):

    # Print a number of lines as a patch to stdout, if any of the lines
    # have been marked for change.

    global prev_file
    show = False
    removed = 0
    for line in lines:
        if line.changed:
            show = True
        if not line.include:
            show = True
            removed += 1

    if not show:
        return

    # If this code-block is being modified anyway, then change use of
    # D_FREE_PTR
    for line in lines:
        line.remove_free_ptr()

    # Strip out and pre and post-amble, patch does not like it if these
    # are uneven length, and as patches are not long-lived there is
    # little benefit in keeping them.
    start = len(lines)
    end = 0
    for idx in range(len(lines)):
        line = lines[idx]
        if not (line.changed or not line.include):
            continue
        if idx < start:
            start = idx
        if idx > end:
            end = idx

    lines = lines[start:end+1]

    start_line = int(lines[0].lineno)

    if lines[0].filename != prev_file:
        print('--- a/{}'.format(lines[0].filename))
        print('+++ a/{}'.format(lines[0].filename))
        prev_file = lines[0].filename

    print('@@ -{},{} +{},{} @@'.format(start_line,
                                       len(lines),
                                       start_line,
                                       len(lines) - removed))
    for line in lines:
        if line.changed:
            print('+{}'.format(line.new_text))
            print('-{}'.format(line.text))
        elif not line.include:
            print('-{}'.format(line.text))
        else:
            print(' {}'.format(line.text))

def check_lines(lines):

    for idx in range(len(lines)):
        if not lines[idx].include:
            continue
        elif lines[idx].close_brace and lines[idx-2].conditional_brace:
            lines[idx].drop_line()
            lines[idx-2].remove_brace()

        elif lines[idx].free_var and lines[idx+1].is_assign_null(lines[idx].free_var):
            lines[idx+1].drop_line()

        elif lines[idx].free_var and lines[idx-1].is_cond_on_var(lines[idx].free_var):
            if lines[idx-1].conditional_brace:
                if lines[idx+1].close_brace:
                    lines[idx].shift_left()
                    lines[idx-1].drop_line()
                    lines[idx+1].drop_line()
            else:
                lines[idx].shift_left()
                lines[idx-1].drop_line()

        elif lines[idx].free_var and lines[idx-1].is_cond_part_on_var(lines[idx].free_var):
            if lines[idx-1].conditional_brace:
                if lines[idx+1].close_brace:
                    lines[idx-1].remove_brace()
                    lines[idx+1].drop_line()
            lines[idx-1].shorten_cond(lines[idx].free_var)

    show_patch(lines)

def main():
    """Check the source tree for D_FREE() usage.

    Check all D_FREE uses, and output a patch to remove any conditional calls
    to improve logging.
    """

    rc = subprocess.run(CMD, stdout=subprocess.PIPE)

    stdout = rc.stdout.decode('utf-8')

    match_lines = []
    for line in stdout.splitlines():
        if line == '--':
            check_lines(match_lines)
            match_lines = []
            continue
        match_lines.append(code_line(line))

if __name__ == '__main__':
    main()
