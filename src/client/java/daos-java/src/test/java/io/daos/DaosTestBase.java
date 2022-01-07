package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "767058ec-9bed-4304-bbd4-e54aabea7bb0";
  public static final String DEFAULT_CONT_ID = "9a95c6a4-ddde-4eb7-a268-636af57f6ace";

  public static final String DEFAULT_POOL_LABEL = "pool1";
  public static final String DEFAULT_CONT_LABEL = "cont1";

  public static final String DEFAULT_OBJECT_CONT_ID = "041f5428-8c8f-4b1c-98de-8dfb2f5f2f5d";

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
