// Code generated by linux/mkall.go generatePtraceRegSet("arm64"). DO NOT EDIT.

package unix

import "unsafe"

// PtraceGetRegSetArm64 fetches the registers used by arm64 binaries.
func PtraceGetRegSetArm64(pid, addr int, regsout *PtraceRegsArm64) error {
	iovec := Iovec{(*byte)(unsafe.Pointer(regsout)), uint64(unsafe.Sizeof(*regsout))}
	return ptracePtr(PTRACE_GETREGSET, pid, uintptr(addr), unsafe.Pointer(&iovec))
}

// PtraceSetRegSetArm64 sets the registers used by arm64 binaries.
func PtraceSetRegSetArm64(pid, addr int, regs *PtraceRegsArm64) error {
	iovec := Iovec{(*byte)(unsafe.Pointer(regs)), uint64(unsafe.Sizeof(*regs))}
	return ptracePtr(PTRACE_SETREGSET, pid, uintptr(addr), unsafe.Pointer(&iovec))
}
