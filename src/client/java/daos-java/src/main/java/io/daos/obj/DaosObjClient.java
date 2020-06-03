package io.daos.obj;

import io.daos.DaosClient;
import io.daos.ForceCloseable;

import java.io.IOException;

public class DaosObjClient implements ForceCloseable {


  @Override
  public void forceClose() throws IOException {

  }

  @Override
  public void close() throws IOException {

  }

  public static class DaosObjClientBuilder extends DaosClient.DaosClientBuilder<DaosObjClientBuilder> {

  }
}
