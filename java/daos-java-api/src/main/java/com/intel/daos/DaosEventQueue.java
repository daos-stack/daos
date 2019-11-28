package com.intel.daos;

import java.io.IOException;
import java.util.LinkedList;
import java.util.Queue;

/**
 * This class correspond to event queue in daos, used for asynchronous APIs.
 * Currently only objects async APIs are supported. No THREAD-SAFETY!
 */
public class DaosEventQueue {
  private long eqh;
  private final Queue<Long> queue;

  public DaosEventQueue() throws IOException {
    queue = new LinkedList<>();
    eqh = DaosJNI.daosEventQueueCreate();
  }

  protected void enqueue(long ioreq) {
    synchronized (queue) {
      queue.add(ioreq);
    }
  }

  public void waitFor(int num) {
    if (num > queue.size()) {
      return;
    }
    DaosJNI.daosEventPoll(eqh, num);
    for (int i = 0; i < num; i++) {
      DaosJNI.free(queue.remove());
    }
  }

  public void waitAll() {
    if (queue.isEmpty()) {
      return;
    }
    waitFor(queue.size());
  }

  protected long getHandle() {
    return eqh;
  }
}