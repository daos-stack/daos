package com.intel.daos.hadoop.fs;

import com.intel.daos.*;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;
import java.util.concurrent.ConcurrentHashMap;

/**
 *
 */
public class CreateDaosFS {
  private FileSystem fs = null;
  private DaosPool pool = null;
  private DaosContainer container = null;
  private DaosFS daosFS = null;

  public CreateDaosFS(){
    createDaosFS();
  }

  private void createFS(){
    Configuration conf = new Configuration();
    conf.set(Constants.DAOS_POOL_UUID, pool.getUuid());
    conf.set(Constants.DAOS_CONTAINER_UUID, pool.getUuid());
    conf.set(Constants.DAOS_POOL_SVC, pool.getSvc());
    fs = TestDaosTestUtils.createTestFileSystem(conf);
  }

  private void createDaosFS(){
    try {
      daosFS = new DaosFS.Builder(true, false, true)
          .setScm(50*1024*1024)
          .setNvme(0)
          .setDaosPoolMode(DaosPoolMode.DAOS_PC_RW)
          .setDaosContainerMode(DaosContainerMode.DAOS_COO_RW).build();
    }catch (IOException e) {
      e.printStackTrace();
    }
  }



  private void closeContainers(){
    if(pool != null){
      ConcurrentHashMap<String, DaosContainer> concurrentHashMap = pool.getContainers();
      for(String key : concurrentHashMap.keySet()){
        DaosContainer cont= concurrentHashMap.get(key);
        cont.closeCont();
      }
    }
  }

  private void destroyPool(){
    if(pool != null){
      pool.destroyPool();
    }
  }

  private void disconnectPool(){
    if(pool != null){
      pool.disconPool();
    }
  }

  public  FileSystem getFs() {
    if(fs == null){
      createFS();
    }
    return fs;
  }

  public  DaosPool getPool(){
    if(pool == null){
      pool = daosFS.getMypool();
    }
    return pool;
  }

  public DaosContainer getContainer(){
    if(container == null){
      container = daosFS.getMycontainer();
    }
    return container;
  }

  public void close(){
    closeContainers();
    disconnectPool();
    destroyPool();
  }
}
