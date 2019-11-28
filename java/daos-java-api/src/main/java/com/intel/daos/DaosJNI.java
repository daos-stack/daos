package com.intel.daos;

import java.nio.ByteBuffer;

/**
 * This class contains all native methods in Daos Java interface.
 * Users shall not directly access these methods, instead users should use
 * other wrapped interfaces for most safety.
 */
public final class DaosJNI {

  /**
   * Initialize daos. Invoke exactly once per process.
   * This method is here only for debug purpose, usually initialized in
   * JNIOnLoad.
   *
   * @return error code
   */
  private static native int daosInit();

  /**
   * Finalize Daos. Invoke exactly once per process after initialization.
   * This method is here only for debug purpose, usually initialized in
   * JNIOnUnload.
   *
   * @return error code
   */
  private static native int daosFinish();

  /**
   * Allocate a ioreq in C using malloc. Initialize an event and store it
   * in the ioreq.
   *
   * @param keysLength: sum of length of dkey and akey
   * @param eqh:        handle of event queue
   * @return pointer to allocated ioreq
   */
  protected static native long allocateIOReq(
      int keysLength,
      long eqh) throws DaosNativeException;

  /**
   * free in C.
   *
   * @param pointer: pointer to memory to be freed
   */
  protected static native void free(long pointer);

// Pool API

  /**
   * Create a pool in daos.
   *
   * @param scm:  size of scm
   * @param nvme: size of nvme
   * @return uuid of created pool
   */
  protected static native String daosPoolCreate(
      long scm,
      long nvme) throws DaosNativeException;

  /**
   * Connect to an existing pool in daos.
   *
   * @return handle of pool
   */
  protected static native long daosPoolConnect(
      String poolUUID,
      int mode,
      String svc) throws DaosNativeException;

  /**
   * destory  a pool in daos.
   *
   * @param poolUUID:  the uuid of pool
   * @return error code
   */
  protected static native int daosPoolDestroy(
      String poolUUID) throws DaosNativeException;


  /**
   * Close the connection to a pool.
   *
   * @param poh: handle of the pool
   */
  protected static native int daosPoolDisconnect(long poh);

// Container API

  /**
   * Create a container in daos.
   *
   * @param poh:  handle of pool where container will be created
   * @param uuid: should be generated randomly(not existing ones)
   * @return error code
   */
  protected static native int daosContCreate(
      long poh,
      String uuid);

  /**
   * Open an existing container in daos.
   *
   * @param poh:  handle of pool where container is
   * @param uuid: uuid of the container
   * @return handle of the opened container
   */
  protected static native long daosContOpen(
      long poh,
      String uuid,
      int mode) throws DaosNativeException;

  /**
   * Close container with its handle.
   *
   * @param coh: handle of container
   * @return error code
   */
  protected static native int daosContClose(long coh);

// Event queue API

  /**
   * Create a event queue in daos.
   *
   * @return handle of event queue
   */
  protected static native long daosEventQueueCreate()
      throws DaosNativeException;

  /**
   * Wait given number events in given event queue to finish.
   *
   * @param eq:  handle of event queue
   * @param num: number of events you want to wait
   * @return error code (might be error code of tasks if negative)
   */
  protected static native int daosEventPoll(
      long eq,
      int num);

// Object API

  /**
   * Open an object in daos.
   *
   * @param poh:   handle of pool
   * @param coh:   handle of container
   * @param id:    unique id in an container
   * @param mode:  mode for opening object in C
   * @param ofeat: feature code of object
   * @param cid:   id of object class in C
   * @return handle of object
   */
  protected static native long daosObjectOpen(
      long poh,
      long coh,
      long id,
      int mode,
      int ofeat,
      int cid) throws DaosNativeException;

  /**
   * Close the object.
   *
   * @param oh: handle of object
   * @return error code
   */
  protected static native int daosObjectClose(long oh);

  /**
   * Fetch single value of corresponding dkey and akey in object.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param buffer: direct byte buffer to put fetched data
   * @return error code
   */
  protected static native long daosObjectFetchSingle(
      long oh,
      String dkey,
      String akey,
      ByteBuffer buffer) throws DaosNativeException;

  /**
   * Fetch single value of corresponding dkey and akey in object
   * non-blocking.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param buffer: direct byte buffer with data to write into object
   * @param ioreq:  result of allocateIOReq
   * @return error code
   */
  protected static native int daosObjectFetchSingleAsync(
      long oh,
      String dkey,
      String akey,
      ByteBuffer buffer,
      long ioreq) throws DaosNativeException;

  /**
   * Fetch (part of) array of corresponding dkey and akey in object.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param offset: offset start to fetch
   * @param number: number of records to fetch
   * @param buffer: direct byte buffer to put fetched data
   * @return error code
   */
  @SuppressWarnings("checkstyle:methodlength")
  protected static native long daosObjectFetchArray(
      long oh,
      String dkey,
      String akey,
      long offset,
      long number,
      ByteBuffer buffer) throws DaosNativeException;

  /**
   * Fetch (part of) array of corresponding dkey and akey in object
   * non-blocking.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param offset: offset start fetching
   * @param number: number of records to fetch
   * @param buffer: direct byte buffer with data to write into object
   * @param ioreq:  result of allocateIOReq
   * @return error code
   */
  protected static native int daosObjectFetchArrayAsync(
      long oh,
      String dkey,
      String akey,
      long offset,
      long number,
      ByteBuffer buffer,
      long ioreq) throws DaosNativeException;


  /**
   * Update single value of corresponding dkey and akey in object.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param buffer: direct byte buffer with data to write into object
   * @return error code
   */
  protected static native int daosObjectUpdateSingle(
      long oh,
      String dkey,
      String akey,
      ByteBuffer buffer) throws DaosNativeException;

  /**
   * Update single value of corresponding dkey and akey in object
   * non-blocking.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param buffer: direct byte buffer with data to write into object
   * @param ioreq:  result of allocateIOReq
   * @return error code
   */
  protected static native int daosObjectUpdateSingleAsync(
      long oh,
      String dkey,
      String akey,
      ByteBuffer buffer,
      long ioreq) throws DaosNativeException;

  /**
   * Update (part of) array of corresponding dkey and akey in object.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param offset: offset start fetching
   * @param size:   size of record
   * @param buffer: direct byte buffer with data to write into object
   * @return error code
   */
  @SuppressWarnings("checkstyle:methodlength")
  protected static native int daosObjectUpdateArray(
      long oh,
      String dkey,
      String akey,
      long offset,
      long size,
      ByteBuffer buffer) throws DaosNativeException;

  /**
   * Update (part of) array of corresponding dkey and akey in object
   * non-blocking.
   *
   * @param oh:     handle of object
   * @param dkey:   distribution key
   * @param akey:   attribute key
   * @param offset: offset start fetching
   * @param size:   size of record
   * @param buffer: direct byte buffer with data to write into object
   * @param ioreq:  result of allocateIOReq
   * @return error code
   */
  protected static native int daosObjectUpdateArrayAsync(
      long oh,
      String dkey,
      String akey,
      long offset,
      long size,
      ByteBuffer buffer,
      long ioreq) throws DaosNativeException;

  /**
   * Punch the whold object.
   *
   * @param oh: handle of object
   * @return error code
   */
  protected static native int daosObjectPunch(long oh);

  /**
   * Punch dkeys from the object.
   *
   * @param oh:    handle of object
   * @param dkeys: dkeys to punch
   * @return error code
   */
  protected static native int daosObjectPunchDkeys(
      long oh,
      String[] dkeys);

  /**
   * Punch akeys from dkey in the object.
   *
   * @param oh:    handle of object
   * @param dkey:  dkey to punch keys from
   * @param akeys: akeys to punch
   * @return error code
   */
  protected static native int daosObjectPunchAkeys(
      long oh,
      String dkey,
      String[] akeys);

  /**
   * List distribution keys currently in object.
   *
   * @param oh: handle of object
   * @return dkeys
   */
  protected static native String daosObjectListDkey(long oh)
      throws DaosNativeException;

  /**
   * List attribute keys under a particular dkey in object.
   *
   * @param oh:   handle of object
   * @param dkey: under which distribution key to list
   * @return akeys
   */
  protected static native String daosObjectListAkey(
      long oh,
      String dkey) throws DaosNativeException;

  /**
   * List recxs under a particular dkey in object.
   *
   * @param oh:                handle of object
   * @param dkey:              under which distribution key to list
   * @param akey:              under which attribute key to list
   * @param inIncreasingOrder: true if wants result in increasing order
   * @return recxs
   */
  protected static native int daosObjListRecx(
      long oh,
      String dkey,
      String akey,
      boolean inIncreasingOrder);

// POSIX file system API

  /**
   * Mount file system on the container supplied.
   * For compatibility of multi-thread, the whole struct is copied,
   * dfs should be used in other operations other than Umount.
   *
   * @param poh:      handle of pool
   * @param coh:      handle of container
   * @param readOnly: true if mount the fs as readonly
   * @return error code
   */
  protected static native int daosFSMount(
      long poh,
      long coh,
      boolean readOnly);

  /**
   * Unmount the file system.
   *
   * @return error code
   */
  protected static native int daosFSUmount();

  /**
   * Check if given path exists in dfs.
   *
   * @param path: path to check
   * @return true if given path exists, false otherwise
   */
  protected static native boolean daosFsIfExist(String path)
      throws DaosNativeException;

  /**
   * Check if given path is a directory.
   *
   * @param path: path to check
   * @return true if given path is a directory, false otherwise
   */
  protected static native boolean daosFSIsDir(String path)
      throws DaosNativeException;

  /**
   * Create the directory given by path. If it already exists , this function
   * will throw exception.
   *
   * @param path: path of directory to open
   * @param mode: mode of directory to create, considered only if creating
   * @return handle of created directory
   */
  protected static native long daosFSCreateDir(
      String path,
      int mode) throws DaosNativeException;

  /**
   * Open the directory given by path. If it does not exist yet, or it is not a
   * directory, this function will throw exception.
   *
   * @param path:     path of directory to open
   * @param readOnly: whether open the directory as readonly
   * @return handle of opened directory
   */
  protected static native long daosFSOpenDir(
      String path,
      boolean readOnly) throws DaosNativeException;

  /**
   * Open the file given by path. If it does not exist yet, or it is not an
   * regular file, this function will throw exception.
   *
   * @param path:     path of file to open
   * @param readOnly: whether to open the file as readonly
   * @return handle of opened file
   */
  protected static native long daosFSOpenFile(
      String path,
      boolean readOnly) throws DaosNativeException;

  /**
   * Create the file given by path. If it already exists, this function will
   * throw exception.
   *
   * @param path:      path of file to open
   * @param mode:      mode of file to create, considered. only if creating
   * @param chunkSize: chunk size of daos array under the file
   * @param cid:       class of object under the file
   * @return handle of opened file
   */
  protected static native long daosFSCreateFile(
      String path,
      int mode,
      long chunkSize,
      int cid) throws DaosNativeException;

  /**
   * Close the file or directory.
   *
   * @param handle: handle of file/directory
   * @return error code
   */
  protected static native int daosFSClose(long handle);

  /**
   * Read from the file from offset for length of buffer.
   *
   * @param file:   handle of file
   * @param offset: offset to start to read
   * @param buffer: direct byte buffer to put data read
   * @return size actually read
   */
  protected static native int daosFSRead(
      long file,
      long offset,
      ByteBuffer buffer) throws DaosNativeException;

  /**
   * Write contents in buffer to the file from offset.
   *
   * @param file:   handle of file
   * @param offset: offset to start to write
   * @param buffer: direct byte buffer to write into file
   * @return size written
   */
  protected static native int daosFSWrite(
      long file,
      long offset,
      ByteBuffer buffer,
      int bufferOffset,
      int length) throws DaosNativeException;

  /**
   * Get the size of a file given by path.
   * Needs to lookup the file and release it in C.
   *
   * @param path: path of file
   * @return size of file
   */
  protected static native long daosFSGetSize(String path)
      throws DaosNativeException;

  /**
   * Get the size of a file given by path.
   *
   * @param file: handle of file
   * @return size of file
   */
  protected static native long daosFSGetSize(long file)
      throws DaosNativeException;

  /**
   * List the files under the directory.
   * Needs to lookup the directory and release it in C.
   *
   * @param path: path of directory
   * @return filenames separated by comma
   */
  protected static native String daosFSListDir(String path)
      throws DaosNativeException;

  /**
   * List the files under the directory.
   *
   * @param handle: handle of directory
   * @return filenames separated by comma
   */
  protected static native String daosFSListDir(long handle)
      throws DaosNativeException;

  /**
   * Move and/or rename the file. Will not create directory.
   * Needs to lookup the directories and release them in C.
   *
   * @param path:    path of the file
   * @param newPath: new path after move
   * @return error code
   */
  protected static native int daosFSMove(
      String path,
      String newPath);

  /**
   * Move and/or rename the file. Will not create directory.
   *
   * @param oldParent: handle of directory where the file is currently in
   * @param name:      name of the file
   * @param newParent: handle of directory to move the file into
   * @param newName:   new name of the file
   * @return error code
   */
  protected static native int daosFSMove(
      long oldParent,
      String name,
      long newParent,
      String newName);

  /**
   * Remove a file in daos. If given path is a directory,
   * itself and all things under it will be removed.
   * Needs to lookup the directory and release it in C.
   *
   * @param path: path of the file/directory to remove
   * @return error code
   */
  protected static native int daosFSRemove(String path);

  /**
   * Remove a file in daos. If given path is a directory,
   * itself and all things under it will be removed.
   *
   * @param parent: handle of parent directory of file/directory to remove
   * @param name:   name of directory/file to remove
   * @return error code
   */
  protected static native int daosFSRemove(
      long parent,
      String name);

  //deleted  constructor
  private DaosJNI() {
  }

  protected static final int ENTRY_SHUTDOWN_PRIO = 13;
  protected static final int DFS_OBJ_SHUTDOWN_PRIO = 12;
  protected static final int CONT_SHUTDOWN_PRIO = 11;
  protected static final int POOL_SHUTDOWN_PRIO = 10;

  static {
//    com.github.fommil.jni.JniLoader.load("libdaos_jni.so");
    System.load("/home/daos/libs/libdaos_jni.so");
  }
}