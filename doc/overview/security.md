<a id="4.4"></a>
# Security Model

DAOS uses a flexible security model that seperates authentication from authorization. It is designed to have very minimal impact on the I/O path.

<a id="4.4.1"></a>
## Authentication

The DAOS security model is designed to support different authentication methods. By default, a local agent runs on the client node and authenticats the user process through AUTH_SYS. Authentication can be handle by a third party service like munge or Kerberos.

<a id="4.4.2"></a>
## Authorization

DAOS supports a subset of the NFSv4 ACLs for both pools and containers through the properties API.
