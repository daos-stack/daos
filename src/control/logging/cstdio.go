package logging

/*
 #include <stdio.h>
*/
import "C"
import "github.com/pkg/errors"

// DisableCStdoutBuffering disables C code write to stdout buffering and must be called before any write to stdout.
//
// When a Go program's stdout is connected to a pipe (e.g. redirected or piped to another
// process), C stdout becomes fully-buffered by Unix convention instead of line-buffered,
// which can cause output from C functions such as printf() to be lost or delayed.
// Disabling buffering ensures that C stdout output is flushed immediately, consistent
// with the behavior of Go's own stdout.
//
// References:
//   - https://stackoverflow.com/questions/42634640/using-cgo-why-does-c-output-not-survive-piping-when-golangs-does
//   - https://stackoverflow.com/questions/1716296/why-does-printf-not-flush-after-the-call-unless-a-newline-is-in-the-format-strin
//   - https://stackoverflow.com/questions/3723795/is-stdout-line-buffered-unbuffered-or-indeterminate-by-default
//   - https://groups.google.com/g/comp.lang.c/c/dvRKt-iuO40#
func DisableCStdoutBuffering() error {
	rc, err := C.setvbuf(C.stdout, nil, C._IONBF, 0)
	if rc == 0 {
		return nil
	}

	if err != nil {
		return errors.Wrap(err, "failed to disable C stdout buffering")
	}

	return errors.Errorf("failed to disable C stdout buffering: rc=%d", int(rc))
}
