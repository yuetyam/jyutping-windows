# Jyutping local RPC protocol

`JyutpingRpc.idl` defines the interface for the experimental standalone server. MIDL generates the client and server stubs. Windows RPC encodes the fixed-width fields using NDR, so pointer width, native structure padding, and `wchar_t` size are independent of the wire contract.

## Negotiation and request identity

- Call `Hello` with the range of application protocol versions the client supports. Version 1 returns the exact-spell capability and an opaque 16-byte server instance ID. Product versions and the MIDL interface version are independent of this negotiation.
- Generate a client ID for each client instance. Keep context IDs unique within that client, advance the composition generation on replacement, and advance the input revision when keys change. Use a new request ID for each call. The TSF adapter currently has one context per client and uses the generation to distinguish compositions.
- `Suggest` carries the negotiated version, both instance IDs, the context, the composition generation, the input revision, and the request ID. Match the full echoed identity before using the reply. The server has no composition state. After a restart it rejects requests carrying the previous server instance.
- A disconnect or stale-instance reply invalidates the binding. The adapter negotiates again on the next request and uses that request's current snapshot. The protocol currently has no mutations. Future mutation acknowledgements must follow durable commit, and retries after an uncertain acknowledgement require transactional deduplication or an explicit decision to forgo replay.

## Exact-spell lookup

Version 1 accepts one to nine lowercase ASCII key events in a fixed-size array with zero padding. A success reply contains the number of consumed key events and two separate UTF-8 fields for display text and romanization. Each field has a byte count and a maximum of 256 bytes. An empty reply has zero lengths and zero consumed events. Invalid requests, unsupported versions, stale server instances, and storage failures have separate statuses. RPC transport failures are returned by the client adapter.

## Endpoint and access

The endpoint uses `ncalrpc`, the Windows session number, and the logon SID. The listener grants access to the logon SID and can grant access to one named AppContainer during compatibility testing. Each authenticated call is checked against the server's user SID, logon SID, and session. The client requests local RPC authentication against its user SID. The endpoint reservation allows one listener per logon. The listener registers a 1 KiB call-size limit and configures RPC for four concurrent calls.

The AppContainer grant is a development option. Deployment still needs an authorization policy and activation path that cover the supported hosts.
