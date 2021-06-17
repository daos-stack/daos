# DAOS RAS Events

RAS events can be raised either in the control or data planes. Each event will
have at minimum an ID, Severity and Type as defined in `ras.go`. The canonical
list of RAS Event IDs is maintained in `src/

Events raised in the data plane will be written to the data plane log
(configured in the engine section of the server config file with `log_`
prefixed parameters) and subsequently forwarded to the control plane over
dRPC.

Events raised in the control plane or received over dRPC 
