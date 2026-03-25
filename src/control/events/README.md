# DAOS RAS Events

RAS events can be raised either in the control or engines. Each event will
have at minimum an ID, Severity and Type as defined in `ras.go`. The canonical
list of RAS Event IDs is maintained in `src/include/daos_srv/ras.h`.

Events raised in the engine will be written to the engine log file
(configured in the engine section of the server config file with `log_`
prefixed parameters) and subsequently forwarded to the control server over
dRPC.

Events raised in the control server or received over dRPC will be written to
syslog at a relevant priority level.

A subset of events are actionable (type 'STATE_CHANGE' as opposed to
'INFO_ONLY') and will be forwarded to the management service (MS) leader. On
receipt of an actionable event, the MS will update the membership and backing
database based on the event's contents.
