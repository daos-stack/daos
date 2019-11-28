package com.intel.daos;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.FileAlreadyExistsException;

import java.io.FileNotFoundException;
import java.io.IOException;

/**
 * daos exception tool.
 */
public final class DaosUtils {
  private DaosUtils(){}
  private static final Log LOG = LogFactory.getLog(DaosUtils.class);

  public static final long DEFAULT_DAOS_POOL_SCM = 2147483648L;
  public static final long DEFAULT_DAOS_POOL_NVME = 8589934592L;

  public static IOException translateException(int status, String message) {

    switch(status){
    case 1:
      return new DaosNativeException("DER_ERR_BASE : Operation not permitted");
    case 2:
      return new DaosNativeException("DER_ERR_BASE : No such file or directory");
    case 3:
      return new DaosNativeException("DER_ERR_BASE : No such process");
    case 4:
      return new DaosNativeException("DER_ERR_BASE : Interrupted system call");
    case 5:
      return new DaosNativeException("DER_ERR_BASE : I/O error");
    case 6:
      return new DaosNativeException("DER_ERR_BASE : Argument list too long ");
    case 7:
      return new DaosNativeException("DER_ERR_BASE : Argument list too long");
    case 8:
      return new DaosNativeException("DER_ERR_BASE : Exec format error");
    case 9:
      return new DaosNativeException("DER_ERR_BASE : Bad file number");
    case 10:
      return new DaosNativeException("DER_ERR_BASE : No child processes");
    case 11:
      return new DaosNativeException("DER_ERR_BASE : Try again");
    case 12:
      return new DaosNativeException("DER_ERR_BASE : Out of memory");
    case 13:
      return new DaosNativeException("DER_ERR_BASE : Permission denied");
    case 14:
      return new DaosNativeException("DER_ERR_BASE : Bad address");
    case 15:
      return new DaosNativeException("DER_ERR_BASE : Block device required");
    case 16:
      return new DaosNativeException("DER_ERR_BASE : Device or resource busy");
    case 17:
      return new DaosNativeException("DER_ERR_BASE : File exists ");
    case 18:
      return new DaosNativeException("DER_ERR_BASE : Cross-device link");
    case 19:
      return new DaosNativeException("DER_ERR_BASE : No such device");
    case 20:
      return new DaosNativeException("DER_ERR_BASE : Not a directory");
    case 21:
      return new DaosNativeException("DER_ERR_BASE : Is a directory");
    case 22:
      return new DaosNativeException("DER_ERR_BASE : Invalid argument");
    case 23:
      return new DaosNativeException("DER_ERR_BASE : File table overflow");
    case 24:
      return new DaosNativeException("DER_ERR_BASE : Too many open files");
    case 25:
      return new DaosNativeException("DER_ERR_BASE : Not a typewriter");
    case 26:
      return new DaosNativeException("DER_ERR_BASE : Text file busy");
    case 27:
      return new DaosNativeException("DER_ERR_BASE : File too large");
    case 28:
      return new DaosNativeException("DER_ERR_BASE : No space left on device");
    case 29:
      return new DaosNativeException("DER_ERR_BASE : Illegal seek");
    case 30:
      return new DaosNativeException("DER_ERR_BASE : Read-only file system");
    case 31:
      return new DaosNativeException("DER_ERR_BASE : Too many links");
    case 32:
      return new DaosNativeException("DER_ERR_BASE : Broken pipe");
    case 33:
      return new DaosNativeException("DER_ERR_BASE : Math argument out of domain of func");
    case 34:
      return new DaosNativeException("DER_ERR_BASE : Math result not representable");
    case -1001:
      return new DaosNativeException("DER_ERR_GURT_BASE : no permission");
    case -1002:
      return  new DaosNativeException("DER_ERR_GURT_BASE : invalid handle");
    case -1003:
      return  new DaosNativeException("DER_ERR_GURT_BASE : invalid parameters");
    case -1004:
      return  new FileAlreadyExistsException("DER_ERR_GURT_BASE : entity already exists");
    case -1005:
      return  new FileNotFoundException("DER_ERR_GURT_BASE : nonexistent entity");
    case -1006:
      return  new DaosNativeException("DER_ERR_GURT_BASE : unreachable node");
    case -1007:
      return  new DaosNativeException("DER_ERR_GURT_BASE : no space on storage target");
    case -1008:
      return  new DaosNativeException("DER_ERR_GURT_BASE : already did sth");
    case -1009:
      return  new DaosNativeException("DER_ERR_GURT_BASE : NO memory");
    case -1010:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Function not implemented");
    case -1011:
      return  new DaosNativeException("DER_ERR_GURT_BASE : timed out");
    case -1012:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Busy");
    case -1013:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Try again");
    case -1014:
      return  new DaosNativeException("DER_ERR_GURT_BASE : incompatible protocol");
    case -1015:
      return  new DaosNativeException("DER_ERR_GURT_BASE : not initialized");
    case -1016:
      return  new DaosNativeException("DER_ERR_GURT_BASE : buffer too short (larger buffer needed)");
    case -1017:
      return  new DaosNativeException("DER_ERR_GURT_BASE : data too long for defined data type or buffer size");
    case -1018:
      return  new DaosNativeException("DER_ERR_GURT_BASE : operation canceled");
    case -1019:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Out-Of-Group or member list");
    case -1020:
      return  new DaosNativeException("DER_ERR_GURT_BASE : transport layer mercury error");
    case -1021:
      return  new DaosNativeException("DER_ERR_GURT_BASE : RPC or protocol version not registered");
    case -1022:
      return  new DaosNativeException("DER_ERR_GURT_BASE : failed to generate an address string");
    case -1023:
      return  new DaosNativeException("DER_ERR_GURT_BASE : PMIx layer error");
    case -1024:
      return  new DaosNativeException("DER_ERR_GURT_BASE : IV callback - cannot handle locally");
    case -1025:
      return  new DaosNativeException("DER_ERR_GURT_BASE : miscellaneous error");
    case -1026:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Bad path name");
    case -1027:
      return  new DaosNativeException("DER_ERR_GURT_BASE : Not a directory");
    case -1028:
      return  new DaosNativeException("DER_ERR_GURT_BASE : corpc failed");
    case -1029:
      return  new DaosNativeException("DER_ERR_GURT_BASE : no rank is subscribed to RAS");
    case -1030:
      return  new DaosNativeException("DER_ERR_GURT_BASE : service group not attached");
    case -1031:
      return  new DaosNativeException("DER_ERR_GURT_BASE : version mismatch");
    case -1032:
      return  new DaosNativeException("DER_ERR_GURT_BASE : rank has been evicted");
    case -1033:
      return  new DaosNativeException("DER_ERR_GURT_BASE : user-provided RPC handler didn't send reply back");
    case -1034:
      return  new DaosNativeException("DER_ERR_GURT_BASE : denial-of-service");
    case -2001:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Generic I/O error");
    case -2002:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Memory free error");
    case -2003:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Entry not found");
    case -2004:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Unknown object type");
    case -2005:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Unknown object schema");
    case -2006:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Object is not local");
    case -2007:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : stale pool map version");
    case -2008:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Not service leader");
    case -2009:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Target create error");
    case -2010:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Epoch is read-only");
    case -2011:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Epoch is too old, all data have been recycled");
    case -2012:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Key is too large");
    case -2013:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Record is too large");
    case -2014:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : IO buffers can't match object extents");
    case -2015:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Event queue is busy");
    case -2016:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Domain of cluster component can't match");
    case -2017:
      return  new DaosNativeException("DER_ERR_DAOS_BASE : Service should shut down");
    default:
      return  new DaosNativeException("no such exception ; status = "+ status);
    }
  }
}
