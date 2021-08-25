package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "fa659243-b141-4dc8-8487-13d8fbd54010";
  public static final String DEFAULT_CONT_ID = "b72e068f-ba04-4546-b05c-2de9d936895e";

  public static final String DEFAULT_POOL_LABEL = "pool1";
  public static final String DEFAULT_CONT_LABEL = "cont1";

  public static final String DEFAULT_OBJECT_CONT_ID = "70be2f5e-5117-41e5-84b7-dba0c9f3601a";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getPoolLabel() {
    return System.getProperty("pool_label", DaosTestBase.DEFAULT_POOL_LABEL);
  }

  public static String getContLabel() {
    return System.getProperty("cont_label", DaosTestBase.DEFAULT_CONT_LABEL);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
