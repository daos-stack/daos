/*
 * (C) Copyright 2018-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package io.daos.obj;

import io.daos.DaosClient;
import io.daos.ForceCloseable;
import io.daos.SharableClient;
import org.apache.commons.lang.ObjectUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class DaosObjClient extends SharableClient implements ForceCloseable {

  // keyed by poolId+contId
  private static final Map<String, DaosObjClient> pcObjMap = new ConcurrentHashMap<>();

  private static final Logger log = LoggerFactory.getLogger(DaosObjClient.class);

  private DaosObjClient(String poolId, String contId, DaosObjClientBuilder builder) {
    super(poolId, contId, builder);
  }

  private synchronized void init() throws IOException {
    if (isInited()) {
      return;
    }
    DaosObjClientBuilder builder = getBuilder();
    setClient(builder.buildDaosClient());
    getClient().registerForShutdown(this);
    setInited(true);
    log.info("DaosObjClient for {}, {} initialized", builder.getPoolId(), builder.getContId());
  }

  @Override
  protected synchronized void disconnect(boolean force) throws IOException {
    decrementRef();
    DaosObjClientBuilder builder = getBuilder();
    if ((force || getRefCnt() <= 0) && isInited()) {
      if (force) {
        getClient().forceClose();
      } else {
        getClient().close();
      }
      log.info("DaosObjClient for {}, {} disconnected", builder.getPoolId(), builder.getContId());
    }
    setInited(false);
    pcObjMap.remove(builder.getPoolId() + builder.getContId());
  }

  @Override
  protected DaosObjClientBuilder getBuilder() {
    return (DaosObjClientBuilder)super.getBuilder();
  }

  public static class DaosObjClientBuilder extends DaosClient.DaosClientBuilder<DaosObjClientBuilder> {
    private boolean shareFsClient;

    /**
     * share {@link DaosObjClient} instance or not.
     *
     * @param shareFsClient
     * default is true
     * @return DaosFsClientBuilder
     */
    public DaosObjClientBuilder shareFsClient(boolean shareFsClient) {
      this.shareFsClient = shareFsClient;
      return this;
    }

    @Override
    public DaosObjClientBuilder clone() throws CloneNotSupportedException {
      return (DaosObjClientBuilder) super.clone();
    }

    @Override
    public DaosObjClient build() throws IOException {
      String poolId = getPoolId();
      String contId = getContId();
      DaosObjClientBuilder builder = (DaosObjClientBuilder) ObjectUtils.clone(this);
      DaosObjClient objClient;
      if (!builder.shareFsClient) {
        objClient = new DaosObjClient(poolId, contId, builder);
      } else {
        //check existing client
        if (poolId == null) {
          throw new IllegalArgumentException("need pool UUID.");
        }
        if (contId == null) {
          throw new IllegalArgumentException("need container UUID.");
        }
        String key = poolId + contId;
        objClient = pcObjMap.get(key);
        if (objClient == null) {
          objClient = new DaosObjClient(poolId, contId, builder);
          pcObjMap.putIfAbsent(key, objClient);
        }
        objClient = pcObjMap.get(key);
      }
      objClient.init();
      objClient.incrementRef();
      return objClient;
    }

    protected DaosClient buildDaosClient() throws IOException {
      return (DaosClient) super.build();
    }
  }
}
