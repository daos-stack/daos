package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "bcc5099c-2ae5-4790-85d2-f625aa8d3065";
  public static final String DEFAULT_CONT_ID = "8c00ed27-1b18-4c92-a95f-17aa672f7fa9";

  public static final String DEFAULT_POOL_LABEL = "pool1";
  public static final String DEFAULT_CONT_LABEL = "cont1";

  public static final String DEFAULT_OBJECT_CONT_ID = "4a61bbf9-d768-4b8a-80ad-ac1e70452b79";

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
