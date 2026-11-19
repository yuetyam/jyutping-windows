# Jyutping local RPC protocol

`JyutpingRpc.idl` defines the interface for the experimental standalone server. MIDL generates the client and server stubs. Windows RPC encodes the fixed-width fields using NDR, so pointer width, native structure padding, and `wchar_t` size are independent of the wire contract.

## Negotiation and request identity

- Call `Hello` with the range of application protocol versions the client supports. The listener selects the highest shared version and returns an opaque 16-byte server instance ID. Version 1 advertises exact-spell lookup. Version 2 also advertises candidate-list lookup. The operations retain their respective request version fields. MIDL interface 1.1 appends the candidate-list operation after the original two operation numbers.
- Generate a client ID for each client instance. Keep context IDs unique within that client, advance the composition generation on replacement, and advance the input revision when keys change. Use a new request ID for each call. The TSF adapter currently has one context per client and uses the generation to distinguish compositions.
- `Suggest` carries the negotiated version, both instance IDs, the context, the composition generation, the input revision, and the request ID. Match the full echoed identity before using the reply. The server has no composition state. After a restart it rejects requests carrying the previous server instance.
- A disconnect or stale-instance reply invalidates the binding. The adapter negotiates again on the next query and uses that query's current snapshot. Mutations are scoped to the instance that returned the selected lexicon and are submitted once.

## Exact-spell lookup

Version 1 accepts one to nine lowercase ASCII key events in a fixed-size array with zero padding. A success reply contains the number of consumed key events and two separate UTF-8 fields for display text and romanization. Each field has a byte count and a maximum of 256 bytes. An empty reply has zero lengths and zero consumed events. Invalid requests, unsupported versions, stale server instances, and storage failures have separate statuses. RPC transport failures are returned by the client adapter.

## Candidate lists

`SuggestCandidates` uses version 2 and echoes the same request identity. The input snapshot carries one to twelve ASCII QWERTY keys in `a`–`z`, `0`–`9`, or apostrophe, with the unused bytes set to zero. The mode selects normal Jyutping, Pinyin, Cangjie 5, stroke, or structure. For a reverse request, `input` holds the query keys. Set `triggerKey` to the matching `r`, `v`, `x`, or `q` to include Windows plain-text matches for the full buffer. A zero trigger requests the reverse engine directly.

`characterStandard` uses the Windows engine values 1 through 9 or 51. `romanizationForm` uses 1 for full, 2 for toneless, and 3 for hidden. Each result contains its type, consumed query key count, display text, display comment, reading mark, original text, and original romanization. Text lengths count UTF-8 bytes. The TSF client accounts for the local reverse trigger separately when consuming input.

`resultLimit` requests the first 1 to 16 candidates in engine order. `hasMore` reports remaining candidates. The opt-in TSF client displays the returned prefix. An empty or failed result carries zero candidates. Paging and work cancellation are follow-up protocol operations.

## Input memory

A version 2 listener with input memory includes learned matches and their database generation in normal candidate lists. `MutateMemory` accepts remember or forget with the original lexicon text, romanization, and that generation. It also accepts clear, which deletes learned words and increments the generation in a single transaction. A write carrying a generation from before a clear or import returns `JyutpingStaleMemory`. The server acknowledges success after SQLite commits. The client treats a transport failure as an uncertain acknowledgement and issues no replay.

## Preferences

A listener started with `--settings`, `--settings-key`, or `--installed` offers `GetSettings` and `SetSetting`. Snapshots contain eight DWORDs in the order defined by the Windows `SettingKind` enum and a 64-bit revision scoped to the server instance. A fetch with the latest revision returns `JyutpingNotModified`. Writes validate a single field and return the resulting snapshot. The server re-reads durable preferences on fetch so a client can reconcile external registry changes. The TSF probe polls every second and keeps live input mode in the TSF compartments.

## Endpoint and access

The endpoint uses `ncalrpc`, the Windows session number, and the logon SID. The listener grants access to the logon SID and can grant access to one named AppContainer during compatibility testing. Each authenticated call is checked against the server's user SID, logon SID, and session. The client requests local RPC authentication against its user SID. The endpoint reservation allows one listener per logon. The listener registers a 1 KiB call-size limit and configures RPC for four concurrent calls.

The AppContainer grant is a development option. Deployment still needs an authorization policy and activation path that cover the supported hosts.
