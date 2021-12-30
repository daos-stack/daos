/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos;

import java.io.IOException;
import java.util.Map;

/**
 * An abstract class for sharing client per pool/container and JVM. It uses {@link DaosClient} for
 * pool/container related operations, as well as registering clients for resource cleanup.
 * It also maintains state of client, like initialized and reference count, so that client can be
 * correctly shared and closed.
 */
public abstract class ShareableClient extends Shareable implements ForceCloseable {

  private String poolId;

  private String contId;

  private DaosClient client;

  private DaosClient.DaosClientBuilder builder;

  protected ShareableClient(String poolId, String contId, DaosClient.DaosClientBuilder builder) {
    this.poolId = poolId;
    this.contId = contId;
    this.builder = builder;
  }

  public String getPoolId() {
    return poolId;
  }

  protected void setPoolId(String poolId) {
    this.poolId = poolId;
  }

  public String getContId() {
    return contId;
  }

  protected void setContId(String contId) {
    this.contId = contId;
  }

  protected DaosClient getClient() {
    return client;
  }

  protected void setClient(DaosClient client) {
    this.client = client;
  }

  protected DaosClient.DaosClientBuilder getBuilder() {
    return builder;
  }

  protected void setBuilder(DaosClient.DaosClientBuilder builder) {
    this.builder = builder;
  }

  public Map<String, String> getUserDefAttributes() {
    return client.getUserDefAttrMap();
  }

  /**
   * close client if there is no more reference to this client.
   *
   * @throws IOException
   */
  @Override
  public synchronized void close() throws IOException {
    disconnect(false);
  }

  /**
   * close client no matter what circumstance is.
   *
   * @throws IOException
   */
  @Override
  public synchronized void forceClose() throws IOException {
    disconnect(true);
  }

  /**
   * disconnect client with all resources being released.
   *
   * @param force
   * disconnect client forcibly?
   * @throws IOException
   */
  protected abstract void disconnect(boolean force) throws IOException;

  @Override
  public String toString() {
    return "SharableClient{" +
        "poolId='" + poolId + '\'' +
        ", contId='" + contId + '\'' +
        ", client=" + client +
        ", inited=" + isInited() +
        ", refCnt=" + getRefCnt() +
        '}';
  }
}
