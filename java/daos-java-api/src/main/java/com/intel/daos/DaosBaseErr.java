package com.intel.daos;

/**
 * Daos base error .
 */
@SuppressWarnings("checkstyle:LineLength")
public enum  DaosBaseErr{
    SUCCESS(0),
    EPERM(1),              /* Operation not permitted */
    ENOENT(2),             /* No such file or directory */
    ESRCH(3),              /* No such process */
    EINTR(4),              /* Interrupted system call */
    EIO(5),                /* I/O error */
    ENXIO(6),              /* No such device or address */
    E2BIG(7),              /* Argument list too long */
    ENOEXEC(8),            /* Exec format error */
    EBADF(9),              /* Bad file number */
    ECHILD(10),            /* No child processes */
    EAGAIN(11),            /* Try again */
    ENOMEM(12),            /* Out of memory */
    EACCES(13),            /* Permission denied */
    EFAULT(14),            /* Bad address */
    ENOTBLK(15),           /* Block device required */
    EBUSY(16),             /* Device or resource busy */
    EEXIST(17),            /* File exists */
    EXDEV(18),             /* Cross-device link */
    ENODEV(19),            /* No such device */
    ENOTDIR(20),           /* Not a directory */
    EISDIR(21),            /* Is a directory */
    EINVAL(22),            /* Invalid argument */
    ENFILE(23),            /* File table overflow */
    EMFILE(24),            /* Too many open files */
    ENOTTY(25),            /* Not a typewriter */
    ETXTBSY(26),           /* Text file busy */
    EFBIG(27),             /* File too large */
    ENOSPC(28),            /* No space left on device */
    ESPIPE(29),            /* Illegal seek */
    EROFS(30),             /* Read-only file system */
    EMLINK(31),            /* Too many links */
    EPIPE(32),             /* Broken pipe */
    EDOM(33),              /* Math argument out of domain of func */
    ERANGE(34),            /* Math result not representable */
    DER_NO_PERM(-1001),      /* no permission */
    DER_NO_HDL(-1002),       /* invalid handle */
    DER_INVAL(-1003),        /* invalid parameters */
    DER_EXIST(-1004),        /* entity already exists */
    DER_NONEXIST(-1005),     /* nonexistent entity */
    DER_UNREACH(-1006),     /* unreachable node */
    DER_NOSPACE(-1007),     /* no space on storage target */
    DER_ALREADY(-1008),     /* already did sth */
    DER_NOMEM(-1009),       /* NO memory */
    DER_NOSYS(-1010),       /* Function not implemented */
    DER_TIMEDOUT(-1011),     /* timed out */
    DER_BUSY(-1012),          /* Busy */
    DER_AGAIN(-1013),       /* Try again */
    DER_PROTO(-1014),       /* incompatible protocol */
    DER_UNINIT(-1015),       /* not initialized */
    DER_TRUNC(-1016),       /* buffer too short (larger buffer needed) */
    DER_OVERFLOW(-1017),        /* data too long for defined data type or buffer size */
    DER_CANCELED(-1018),     /* operation canceled */
    DER_OOG(-1019),         /* Out-Of-Group or member list */
    DER_HG(-1020),         /* transport layer mercury error */
    DER_UNREG(-1021),       /* RPC or protocol version not registered */
    DER_ADDRSTR_GEN(-1022),   /* failed to generate an address string */
    DER_PMIX(-1023),          /* PMIx layer error */
    DER_IVCB_FORWARD(-1024),   /* IV callback - cannot handle locally */
    DER_MISC(-1025),       /* miscellaneous error */
    DER_BADPATH(-1026),       /* Bad path name */
    DER_NOTDIR(-1027),        /* Not a directory */
    DER_CORPC_INCOMPLETE(-1028),    /* corpc failed */
    DER_NO_RAS_RANK(-1029),   /* no rank is subscribed to RAS */
    DER_NOTATTACH(-1030),       /* service group not attached */
    DER_MISMATCH(-1031),        /* version mismatch */
    DER_EVICTED(-1032),       /* rank has been evicted */
    DER_NOREPLY(-1033),       /* user-provided RPC handler didn't send reply back */
    DER_DOS(-1034),         /* denial-of-service */
    DER_IO(-2001),          /* Generic I/O error */
    DER_FREE_MEM(-2002),        /* Memory free error */
    DER_ENOENT(-2003),        /* Entry not found */
    DER_NOTYPE(-2004),        /* Unknown object type */
    DER_NOSCHEMA(-2005),        /* Unknown object schema */
    DER_NOLOCAL(-2006),       /* Object is not local */
    DER_STALE(-2007),         /* stale pool map version */
    DER_NOTLEADER(-2008),       /* Not service leader */
    DER_TGT_CREATE(-2009),      /* * Target create error */
    DER_EP_RO(-2010),        /* Epoch is read-only */
    DER_EP_OLD(-2011),        /* Epoch is too old, all data have been recycled */
    DER_KEY2BIG(-2012),       /* Key is too large */
    DER_REC2BIG(-2013),       /* Record is too large */
    DER_IO_INVAL(-2014),        /* IO buffers can't match object extents */
    DER_EQ_BUSY(-2015),       /* Event queue is busy */
    DER_DOMAIN(-2016),        /* Domain of cluster component can't match */
    DER_SHUTDOWN(-2017),        /* Service should shut down */
    DER_INPROGRESS(-2018),      /* Operation now in progress */
    DER_NOTAPPLICABLE(-2019);     /* Not applicable. */

  private final int value;

  DaosBaseErr(int value) {
    this.value = value;
  }

  public int getValue() {
    return value;
  }
}
