/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs;

import java.lang.ref.PhantomReference;
import java.lang.ref.ReferenceQueue;
import java.security.AccessController;
import java.security.PrivilegedAction;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Companion cleaner object for opened {@link DaosFile}. When {@link DaosFile} is phantom-reachable,
 * this companion cleaner object will be put to a internal {@link ReferenceQueue} which is polled
 * repeatedly by a single thread for invoking its {@link #clean()} method.
 *
 * <p>
 * This cleaner is different from {@link sun.misc.Cleaner} that cleaner object cannot be removed once
 * created. It means this {@link Cleaner} object's {@link #clean()} is always invoked. If you don't
 * want actual clean after this object created, like {@code dfs_release()} for {@link DaosFile}, you can put
 * some special logic in {@link Runnable} action.
 *
 * <p>
 * The actual polling and clean is {@link CleanerTask} which is executed in {@link DaosFsClient}.
 *
 * @see DaosFsClient
 */
public class Cleaner extends PhantomReference<Object> {
  private static final ReferenceQueue<Object> dummyQueue = new ReferenceQueue();

  private final Runnable action;

  private Cleaner(Object referent, Runnable action) {
    super(referent, dummyQueue);
    this.action = action;
  }

  protected static Cleaner create(Object referent, Runnable action) {
    if (action == null) {
      return null;
    }
    return new Cleaner(referent, action);
  }

  /**
   * clean object.
   */
  public void clean() {
    try {
      this.action.run();
    } catch (Throwable e) {
      AccessController.doPrivileged((PrivilegedAction<Void>) () -> {
        if (System.err != null) {
          (new Error("Cleaner terminated abnormally", e)).printStackTrace();
        }
        System.exit(1);
        return null;
      });
    }
  }


  /**
   * Cleaner Task to poll {@link Cleaner} object and invoke its clean method.
   */
  protected static class CleanerTask implements Runnable {

    private static final Logger log = LoggerFactory.getLogger(CleanerTask.class);

    @Override
    public void run() {
      int count = 0;
      while (!Thread.interrupted()) {
        Cleaner cleaner = (Cleaner) dummyQueue.poll();
        if (cleaner == null) {
          count++;
          if (count > 16) {
            try { //sleep for a while instead of busy wait
              Thread.sleep(2000);
            } catch (InterruptedException e) {
              if (log.isDebugEnabled()) {
                log.debug("cleaner thread interrupted", e);
              }
              break;
            }
          }
          continue;
        }
        count = 0;
        cleaner.clean();
      }
    }
  }
}
