package com.intel.daos;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * This class has no corresponding object in native daos. It is a singleton used
 * to cache connected pools, to be re-used in functions. Furthermore, multiple
 * threads can share the same handle. When users have done all tasks, they only
 * need to call close(), which will call disconnect all pools, which in turn
 * closes all containers and objects.
 * Notice that DaosFS will need pools and containers be OPEN. Therefore, always
 * call close() in the end, after DaosFS.unmount();
 */
public final class DaosSession {

  private static DaosSession session = null;

  private final ConcurrentHashMap<String, DaosPool> pools;

  private final static ReentrantReadWriteLock RWLOCK = new ReentrantReadWriteLock();

  private final static Lock WL = RWLOCK.writeLock();

  private final static Lock RL = RWLOCK.readLock();

  private final static Logger LOG = LoggerFactory.getLogger(DaosSession.class);

  private DaosSession() {
    pools = new ConcurrentHashMap<>();
  }

  public static DaosSession getInstance() {
    WL.lock();
    try {
      if (session == null) {
        session = new DaosSession();
      }
      return session;
    } finally {
      WL.unlock();
    }
  }

  public void close() {
    WL.lock();
    try {
      if (session != null) {
        pools.forEachValue(1, (DaosPool pool) -> {
          try {
            pool.disconnect();
          } catch (IOException e) {
            LOG.error(e.getMessage());
          }
        });
        session = null;
      }
    } finally {
      WL.unlock();
    }
  }

  public DaosPool createAndGetPool(
      final long scm,
      final long nvme,
      final DaosPoolMode mode) throws IOException {
    String uuid;
    String svc;
    RL.lock();
    try {
      if (session != null) {
        String poolReturn  = DaosJNI.daosPoolCreate(scm, nvme);
        String[] str =poolReturn.split(" ");
        if(str.length!=2){
          throw new DaosJavaException("create pool is fail.");
        }
        uuid = str[0];
        svc = str[1];
        DaosPool pool = new DaosPool(this, uuid, mode, svc);
        pools.put(uuid + "_" + mode.getValue(), pool);
        return pool;
      } else {
        throw new DaosJavaException("Session is already closed.");
      }
    } finally {
      RL.unlock();
    }
  }

  public DaosPool getPool(
      final String uuid,
      final DaosPoolMode mode,
      final String svc) throws IOException {
    String key = uuid + "_" + mode.getValue();
    RL.lock();
    try {
      if (session != null) {
        DaosPool pool = pools.computeIfAbsent(key, (String k) -> {
          try {
            return new DaosPool(this, uuid, mode, svc);
          } catch (DaosNativeException e) {
            return null;
          }
        });
        if (pool == null) {
          throw new DaosNativeException("Failed to connect to pool");
        } else {
          return pool;
        }
      } else {
        throw new DaosJavaException("Session is already closed.");
      }
    } finally {
      RL.unlock();
    }
  }

  protected void deletePool(DaosPool pool) {
    RL.lock();
    try {
      if (session != null) {
        pools.remove(pool.toString() + "_" + pool.getMode().getValue());
      }
    } finally {
      RL.unlock();
    }
  }
}