package faults

// Code represents a stable fault code.
//
// NB: All control plane errors should register their codes in the
// following block in order to avoid conflicts.
type Code int

const (
	// general fault codes
	CodeUnknown Code = iota

	// storage fault codes
	CodeStorageUnknown Code = iota + 100
	CodeStorageAlreadyFormatted
	CodeStorageFilesystemMounted
	CodeStorageFormatCheckFailed

	// security fault codes
	CodeSecurityUnknown Code = iota + 200
)
