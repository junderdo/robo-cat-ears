# Robo Cat Ears BLE protocol

The ears are the BLE peripheral and the GATT **server**. Everything a client can ask of them goes
through one service, `0xABF0`. This document is the contract between three repositories:

| Repo | Role |
| --- | --- |
| `robo-cat-ears` | firmware, GATT server, **owner of record** for this document |
| `robo-cat-ears-watch` | firmware, BLE client — play-only |
| `milk-lab-creations` | SvelteKit web app, BLE client over Web Bluetooth — the authoring tool |

Two halves, and the split is load-bearing:

- **§1–§3** describe the surface that **exists today** in firmware: type bytes `0x01`–`0x05`, all
  fire-and-forget, no replies, no errors.
- **§4–§10** specify the **animation store** — type byte `0x06`. It is transactional: every request
  gets a reply and can fail with an enumerated status.

§11 listed what had to be written to make §4–§10 real; it is now a record of what was, kept because
it names the things most easily mistaken for pre-existing behaviour.

All multi-byte integers on the wire are **big-endian**, matching the existing serializers
(`main/types/custom_animation_types.h`, `main/servo_calibration.c`).

---

## 1. Service, advertising, connection

### 1.1 Advertising

`main/ble.c:58-64` advertises 23 bytes: flags, the complete 16-bit service UUID list containing
`0xABF0`, and the complete local name `ROBO_CAT_EARS`. Both are usable as scan filters — a Web
Bluetooth client may filter on the service UUID alone and skip `optionalServices`, or filter on the
name and declare `0xABF0` as an optional service.

The numeric literal `0xabf0` is a legal `BluetoothServiceUUID` and canonicalises to
`0000abf0-0000-1000-8000-00805f9b34fb`. The *string* `"0xABF0"` is not legal and throws `TypeError`.
Nothing in `0xABF0`–`0xABF5` appears in the Web Bluetooth blocklist.

### 1.2 Characteristics

| UUID | Name in source | Properties | Role |
| --- | --- | --- | --- |
| `0xABF1` | `spp_data_receive` | READ, WRITE_NR (see §11.1) | **Client -> ears.** Every command. |
| `0xABF2` | `spp_data_notify` | READ, INDICATE (see §11.2) + CCCD | **Ears -> client.** Store responses. Its readable value also carries lighting state. |
| `0xABF3` | `spp_command_receive` | READ, WRITE_NR | Declared, **no handler**. Unused. |
| `0xABF4` | `spp_status` | READ, NOTIFY + CCCD | Declared, **no handler**. Unused. |
| `0xABF5` | heartbeat | — | **Absent from this build.** `SUPPORT_HEARTBEAT` is commented out at `main/boot.h:17`. |

A client must not depend on `ABF3`, `ABF4` or `ABF5`. `ABF3` and `ABF4` exist because the firmware
descends from the Espressif BLE-SPP example; `controller_handle_write` dispatches only on the `ABF1`
value handle (`main/controller.c`, `DATA_HANDLE`), so a write to `ABF3` is logged and dropped.

**`ABF2` is multiplexed.** `controller_update_lighting_characteristic` writes serialized lighting
state into the `ABF2` value attribute so a client can *read* it. Store responses are *indicated* on
the same characteristic. This is why outbound store frames carry the same leading type byte as
inbound ones (§5): without it a client cannot tell a store response from a lighting blob.

### 1.3 Connection model

- **One client at a time.** The firmware tracks a single `spp_conn_id`. Connectable advertising stops
  at the controller the moment a connection is established and is restarted on
  `ESP_GATTS_DISCONNECT_EVT` (`main/ble.c:713`), so while a client is connected no other client can
  find the ears. The connection is effectively exclusive.
- **The exclusivity is what removes optimistic concurrency from §4–§10.** Nothing can change the
  store underneath a connected client except that client, so a slot listing read at connect stays
  valid for the session and `STORE` needs no compare-and-swap.
- **Connection parameters**: 7.5–15 ms interval, zero latency, 4 s supervision timeout, requested on
  connect (`main/ble.c:~676`).
- **MTU**: the firmware calls `esp_ble_gatt_set_local_mtu(512)` on connect. Measured against Chrome
  151 on Windows 10, the negotiated ATT MTU is **512**, giving a largest single ATT write of
  **509 bytes**. That is not a theoretical ceiling: a 509-byte custom animation was written and
  played end to end.
- **On disconnect** the firmware resets `spp_mtu_size` to 23, clears `is_connected` and
  `enable_data_ntf`, and resumes advertising. All store reassembly state is discarded (§7.4).

### 1.4 Sizing, and why the protocol carries its own chunk size

**The ATT MTU is invisible to JavaScript.** The Web Bluetooth spec runs Exchange MTU inside
`connect()` and never surfaces the result — no attribute, no getter, and the open CG issues asking
for one (#383, #284) have gone nowhere. Separately, Blink hard-caps a characteristic value at
**512 bytes** and throws `InvalidModificationError` above it.

So the protocol must **tell** the client its usable frame size rather than let the client derive it.
That is what `max_chunk_bytes` in the capability record (§8) is for. Design nothing that reads or
assumes the MTU.

**Writes of 510–512 bytes are predicted to fail silently.** Chrome permits values up to 512, but any
write above `ATT_MTU - 3` becomes a Write Long, which reaches `ESP_GATTS_EXEC_WRITE_EVT`
(`main/ble.c:632`) — a handler that only prints and frees the buffer and **never calls
`controller_handle_write`**. The write resolves cleanly in JavaScript and the application never sees
it. This is a prediction from code, not an observation; the ears must never advertise a
`max_chunk_bytes` above `MTU - 3`, and a client must never send more than the advertised value.

Web Bluetooth also gives no backpressure: `writeValueWithoutResponse` resolves when the value is
handed to the stack, not when it is delivered. There is no `bufferedAmount` and no drain event. This
is the reason the store path uses **write-with-response** (§5.1).

---

## 2. The existing command surface (`0x01`–`0x05`)

Every command is a single write to `0xABF1` shaped as:

```
[type:u8][payload...]
```

`data_packet_unpack` (`main/types/ble_packet_types.h`) rejects an unknown type byte outright. The
command is then queued to the controller task. **None of `0x01`–`0x05` produces a reply.** There is
no ack, no nack and no error path — a malformed payload is logged on the device and the client never
learns. This is the whole reason the store is a new regime rather than more type bytes (§4).

### 2.1 `0x01` — play a built-in animation

```
[0x01][animation_id:u8]
```

`animation_id` is `1`–`8` (`NUM_ANIMATIONS`, `main/animation.h:14`). ASCII digits `'1'`–`'9'` are
also accepted and converted, a quirk of `process_animation_command` retained for the watch's
existing button code. Out-of-range ids are logged and dropped.

### 2.2 `0x02` — lighting

```
[0x02][mode:u8][speed:u8][color_count:u8][r,g,b] * color_count
```

`mode` is `0` solid, `1` breathing, `2` marquee, `3` chasing, `4` rain. `speed` is 1–100.
`color_count` is 0–32. Deserialization requires at least `3 + color_count * 3` bytes.

The ears mirror current lighting state into the readable `ABF2` value attribute, so a client can read
it back — this is the only readable state the service exposes.

### 2.3 `0x03` — servo calibration

```
[0x03][left_azi:i16][left_lat:i16][right_azi:i16][right_lat:i16]
```

Exactly 8 payload bytes, big-endian, each offset in the range −1000..+1000.

### 2.4 `0x04` — animation mode

```
[0x04][mode_id:i16][frequency:i16]
```

Exactly 4 payload bytes. `frequency` is how many times per hour the idle animation fires.

### 2.5 `0x05` — stream and play a custom animation

```
[0x05][transfer_id:u8][chunk_index:u8][chunk_count:u8][payload...]
```

Chunks are concatenated in order into a `CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE` (769 B) buffer; when
`chunk_index + 1 == chunk_count` the buffer is passed to `custom_animation_deserialize` and played.
`chunk_index == 0` supersedes any transfer in flight. An out-of-order index drops the transfer
silently.

**`0x05` survives the store, unchanged and unabsorbed.** It expresses something slot-play cannot:
*play this payload, which is not in the store*. The web editor's "try it on the real ears" preview is
exactly that — the user is iterating on an unsaved edit and must not burn a slot or a flash write per
attempt. It also decouples the rollout: new ears firmware must keep playing animations for a watch
that has not been reflashed.

**The watch has zero `0x05` callers.** `CustomAnimationService::playAnimation()` is deleted as part
of the pure-client change; `0x05` is kept alive for the web app's preview specifically, not "just in
case".

**Do not read `0x05` and `0x06`/`PLAY` as one command with two argument forms.** `0x05` is
fire-and-forget with no reply; play-by-slot is transactional and can fail with `SLOT_OUT_OF_RANGE` or
`SLOT_EMPTY`, and the client must hear that.

### 2.6 The animation wire format

Shared by `0x05` and by `STORE` (§7.3). Produced by `custom_animation_serialize`, consumed by
`custom_animation_deserialize`, and reproduced byte-for-byte by `packWireFormat` in the web app
(`apps/api/src/wire-format.ts`).

```
[keyframe_count:u8]
then keyframe_count times, 12 bytes each:
  [time_ms:u16][angle0:u8][angle1:u8][angle2:u8][angle3:u8]
  [ease_in_type:u8][ease_out_type:u8][ease_in_ms:u16][ease_out_ms:u16]
```

Maximum serialized size is `1 + 64 * 12` = **769 bytes** (`CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE`).

Validity, as enforced by `custom_animation_deserialize`:

- `1 <= keyframe_count <= 64`
- every angle `<= 180`
- `ease_in_type` and `ease_out_type` `<= 3` (0 none/linear, 1 sine, 2 cubic, 3 elastic)
- `time_ms` non-decreasing across keyframes — **equal timestamps are legal** (the check is `<`, not
  `<=`)

Both implementations are pinned to a shared golden fixture covering these edges: `make -C test`
here, `apps/api/test/wire-format-conformance.test.ts` in the web app. The canonical file is
`docs/spec/wire-format-fixture.json` in the web repo; `test/wire-format-fixture.json` is a copy that
`test/check-fixture-drift.sh` keeps honest.

---

## 3. Storage

### 3.1 The `anim` partition

The store lives in its **own NVS partition**, added to a custom partition CSV:

```
anim, data, nvs, 0x110000, 0x10000
```

64 KB, in the previously unallocated tail. `nvs`, `phy_init` and `factory` keep identical offsets, so
existing settings **and the BLE bonding database** survive an ordinary `idf.py flash` — flashing a
new partition table erases nothing.

**Why not the stock `nvs` partition.** The Bluedroid bond database lives there
(`CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=y`, keys `bt_config.conf` / `bt_cfg_key0…`) and grows per bonded
peer, by an amount that is not knowable statically. Animations competing with bonds for one 24 KB
partition is the only risk in this design that cannot be reasoned about ahead of time: it fails in
the field, on the device with the most bonded peers, after the user has filled their slots. Severing
that coupling costs one CSV line and an `nvs_flash_init_partition("anim")` call.

**Honest cost:** the firmware now ships a custom partition table. Flashing from a stale checkout
without the CSV produces a table that does not know about `anim`, and devices already in the field
need the new table flashed, not just the app.

### 3.2 Capacity, and why 16 slots

An NVS page is 4096 B = 32 B header + 32 B entry bitmap + 126 × 32 B entries. One full page is
permanently reserved for compaction. 64 KB = 16 pages -> 15 usable -> **1890 entries**.

A blob costs `2 + ceil(N/32)` entries. An ~805-byte record costs 28, so **16 slots consumes 448
entries** — roughly a quarter of the pool, with the headroom floor a non-issue at this count.

The sizing is deliberately asymmetric: the **partition** generously, because flash in the
unallocated tail is free and resizing it later is destructive to the store; the **slot count**
conservatively, because 16 is the number a human curates. Raising it to 32 or 48 later is a firmware
constant change — no partition change, no data migration. That headroom is the reason `slot_count` is
reported on the wire (§8) instead of being a shared constant.

Rewrite behaviour worth knowing when tuning any of this: the new blob version is written in full
*before* the old one is erased, `writeMultiPageBlob` abandons page tailroom below 400 B, and garbage
collection is lazy — it runs only at the last free page and reclaims one page per call. On a
nearly-full partition a write that fits in the free space can still fail. At 16 slots in 64 KB this
is far from binding.

### 3.3 The slot record

One NVS blob per slot — not three keys. It is 28 entries against 32, and NVS's version scheme makes
a single blob **atomic**: a torn write leaves the old animation or a cleanly empty slot, never a
half-updated one. That atomicity is what §7.3's overwrite guarantee rests on.

Key: the decimal slot index as a string (`"0"` … `"15"`) in the `anim` namespace.

Value:

```
[animation_id:16][name_len:u8][name:name_len][wire_format:...]
```

- `animation_id` — the web app's `Animation.id`, a UUID in RFC 4122 network byte order. **All-zero
  means watch-authored** (i.e. not traceable to a row in the web app's database).
- `name` — UTF-8, `name_len` counted in **bytes**, 1–32.
- `wire_format` — §2.6, up to 769 bytes.

Maximum record: `16 + 1 + 32 + 769` = **818 bytes**.

An **empty slot has no key**. Deleting is `nvs_erase_key`; occupancy is "does the key exist".

---

## 4. The store surface: one type byte, sub-opcodes behind it

```
0x06 = DATA_TYPE_STORE
```

Store commands do **not** claim `0x06`–`0x0C` individually. The first payload byte is a `u8`
sub-opcode.

The type byte marks the **regime change**, not the command. `0x01`–`0x05` are one-way,
fire-and-forget, no reply, no errors. Every store command is the opposite — correlation id, reply on
`ABF2`, enumerated errors. One type byte draws that boundary exactly where it is; scattering store
commands across several type bytes would mean the type byte no longer tells you which regime you are
in. It also keeps the framing header written once, and keeps store growth out of
`types/ble_packet_types.h`, which every subsystem includes.

Cost: one extra byte per request and a two-level dispatch.

---

## 5. Framing

```
Request   (ABF1, write-with-response)
[0x06][corr:u8][sub_opcode:u8][chunk_index:u8][chunk_count:u8][payload...]

Response  (ABF2, indication)
[0x06][corr:u8][status:u8][chunk_index:u8][chunk_count:u8][payload...]
```

Five bytes of header in both directions. **Payload bytes per frame = `max_chunk_bytes - 5`.**

Three properties of this layout are load-bearing:

- **The header is uniform on every request, including single-frame ones** (`chunk_index = 0`,
  `chunk_count = 1`). Reassembly is therefore a **transport concern**: the write handler reassembles
  any `0x06` generically, and sub-opcode dispatch only ever sees a *complete* request. Per-command
  chunk handling is where this class of protocol rots. The cost is 2 bytes on `DELETE`; the
  degenerate case is not a lie, it is the same case.
- **Correlation id and transfer id are one field, `corr`.** They do the same job — identify this
  request across frames — so two fields would be two things that can disagree. Client-chosen, echoed
  in every response frame, never interpreted by the ears. Wrap-around is harmless with one request
  outstanding.
- **`sub_opcode` is repeated in every chunk**, so a stray or late chunk is rejectable without
  consulting reassembly state.

The **response** stream is chunked blindly: list entries may straddle a frame boundary. A parser only
ever operates on the whole reassembled response.

### 5.1 Requests use write-with-response

`ABF1` already carries `ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE` (`main/ble.c:204`) and Bluedroid
enforces permissions rather than the declaration's property bits, so a Write Request is honoured
today despite the missing property bit (§11.1).

Write-with-response gives a real per-chunk ACK from the link layer for free. With
write-without-response a lost write surfaces **no error at all** to JavaScript, which would make
application-level acks, timeouts and retransmit mandatory.

**There is no per-chunk application ack.** ATT already confirms receipt of each chunk; an
application-level ack would double the round trips to re-confirm what the transport just confirmed.
One response at the end — or an early error response that aborts the transfer mid-flight.

### 5.2 Responses are indications, not notifications

`ABF2` is used as an **indication** channel. Indications are ATT-acknowledged and strictly
one-outstanding: the stack will not send frame N+1 until the client confirms N. That is per-frame
flow control and delivery confirmation for free.

The cost is one round trip per frame (~1 connection interval, 7.5–15 ms on this link), so a 2-frame
response costs ~20 ms more than notifications would. Invisible.

From JavaScript the two are indistinguishable: `startNotifications()` writes whichever CCCD bit the
property declares, and both surface as `characteristicvaluechanged`. **The web app needs no code
difference either way.**

Sequence numbers still go in the frame header. The spec must not depend on transport guarantees; with
indications they are an assertion rather than a recovery mechanism.

> **Reconciliation for firmware implementers.** `main/ble.c:154-160` picks the `ABF2` property from
> `CONFIG_EXAMPLE_SPP_THROUGHPUT`: set, it declares READ|NOTIFY; unset, READ|INDICATE. The current
> generated `sdkconfig` has it **set** (`sdkconfig:500`), which is why the hardware spike observed
> Chrome reporting `props: read, notify`. This protocol requires INDICATE, so the build must select
> the `#else` branch — see §11.2. Neither branch is the cheap option: `enable_data_ntf`
> (`main/ble.c:79`) is never assigned `true` anywhere, so the CCCD write handler is new firmware
> regardless.

---

## 6. The mandatory connect sequence

**Subscribe `ABF2` -> `CAPABILITY` -> `LIST` -> operate.** All three, in that order, before any other
store command.

- Subscribing first is what makes responses deliverable, and the ears **refuse to execute any `0x06`
  request while `ABF2`'s CCCD is not set for indications** (§9.1).
- `CAPABILITY` before anything else because `slot_count` and `max_chunk_bytes` are per-connection
  facts the client must not hardcode. There is no bootstrap problem: the `CAPABILITY` request and its
  response both fit in one frame, so nothing needs `max_chunk_bytes` in order to fetch
  `max_chunk_bytes`.
- `LIST` before any upload because there is no server-side slot allocation (§7.3): the client picks
  the target, so it must know what is occupied.

Clients **cache these for the connection and re-read them on every reconnect**. Neither is persisted
across sessions. A cached list must be **tagged with the client's own stable identifier for the device
it came from, and discarded on sight if a new connection's identifier differs** — stale slot indices
from device A silently playing slots on device B is the most dangerous state a client can reach.

The identifier is whatever the client platform provides: `BluetoothDevice.id` on the web, the
reconnect handle on the watch. **Not the serial** (§8): the serial is optional, so tagging with it
would leave every pre-serial device untagged — the one population where the rule is most likely to be
exercised. This previously said "the device address", which named a value no client can see; Web
Bluetooth hides the MAC, and that wording made the rule read as unimplementable when it never was.

---

## 7. Sub-opcodes

```
0x01 CAPABILITY     req: --                        resp: [version:u8][slot_count:u8][max_chunk_bytes:u16][serial:6]
0x02 LIST           req: --                        resp: [entry_count:u8] then entries, ascending by index:
                                                         [index:u8][animation_id:16][name_len:u8][name]
0x03 STORE          req: [slot:u8][animation_id:16][name_len:u8][name][wire_format]   resp: --
0x04 DELETE         req: [slot:u8]                 resp: --
0x05 PLAY           req: [slot:u8]                 resp: --
0x06 GET_ANIMATION  reserved, unimplemented -> UNSUPPORTED_OPCODE
0x07 RENAME         reserved, unimplemented -> UNSUPPORTED_OPCODE
```

`STORE`, `DELETE` and `PLAY` return an **empty payload**. `status` carries the whole answer; an
echoed slot index would only be a second thing that can disagree with the request.

`PLAY = 0x05` deliberately echoes `DATA_TYPE_CUSTOM_ANIMATION = 0x05`; both mean "play".

### 7.1 `CAPABILITY`

See §8.

### 7.2 `LIST`

The response is **sparse**: only occupied slots appear, ascending by index. An empty store is a bare
`[entry_count = 0]`.

```
[entry_count:u8]
[index:u8][animation_id:16][name_len:u8][name:name_len]   * entry_count
```

Occupancy is derived as `slot_count` minus `entry_count` — **never transmitted**, so the two cannot
disagree. `animation_id` all-zero means watch-authored.

The usual argument for a dense array is fixed-size records you can memcpy, but names are
variable-length, so a dense response is *already* a walk-and-parse loop. Density buys nothing
structurally and costs 13 wasted entries in the common mostly-empty case — on the worst path, the
chunked outbound channel.

**There is no paging opcode.** Generic chunking already covers the worst case in 2 frames, and a
paging cursor would be a second chunking mechanism layered on the first, with its own consistency
question about a list mutating between pages.

### 7.3 `STORE`

```
[slot:u8][animation_id:16][name_len:u8][name:name_len][wire_format...]
```

- **The target slot is always explicit.** There is no "any free slot" mode and no `STORE_FULL` status.
  The client has read `LIST` and knows `slot_count`, so it already knows which slots are free; the
  ears cannot tell it anything it does not have. Server-side allocation only earns its keep when the
  server knows something the client does not, and here, by construction, it never does.
  **Fullness is a UI state, not a wire state** — the web app shows 16 slots and makes the user pick a
  victim. The ears never say no.
- **Overwrite is the same operation.** No compare-and-swap, no precondition, no echoing back the
  expected occupant. The connection is exclusive (§1.3), so the list read at connect cannot change
  underneath the client; a CAS token would guard a race the concurrency model has already made
  impossible. If multi-client is ever revisited, an optional expected-id field is a
  backwards-compatible addition.
- **The write is atomic** (§3.3). An interrupted overwrite leaves either the old animation or a
  cleanly empty slot, never a corrupt one.
- **Re-driving a `STORE` is idempotent by construction**, which is what makes "restart at chunk 0"
  a complete recovery story (§7.6).

Validation order, all of it on the ears:

1. **Frame/shape**: payload long enough to contain `slot`, `animation_id`, `name_len` and `name` ->
   else `MALFORMED_REQUEST`.
2. **`slot < slot_count`** -> else `SLOT_OUT_OF_RANGE`.
3. **Name**: 1–32 bytes, well-formed UTF-8, no control characters (`< 0x20`, `0x7F`) -> else
   `INVALID_NAME`. Name validation runs **before** the animation validator so a bad name does not
   cost a full deserialize.
4. **Exact length**: the remaining `wire_format` bytes must equal `1 + keyframe_count * 12`
   **exactly**; trailing bytes are `MALFORMED_REQUEST`.
5. **Animation**: `custom_animation_deserialize` -> else `INVALID_ANIMATION`.
6. **Commit** to NVS -> `STORAGE_FAILURE` on error, `OK` otherwise.

**Nothing is committed unless every step passes.** An `OK` means the animation is in flash.

### 7.4 `DELETE`

```
[slot:u8]
```

Idempotent: deleting an already-empty slot **succeeds** with `OK`, so a client retrying after a
dropped connection needn't reason about whether its first attempt landed. `slot >= slot_count` is
`SLOT_OUT_OF_RANGE`.

**There is no bulk clear.** It is the most destructive thing the protocol could expose and saves at
most 16 round trips on an operation performed approximately never. Additive later if wanted.

Delete exists at all because without it a slot can never return to empty, and the only way to stop
wanting an animation would be to overwrite it with one you want less.

### 7.5 `PLAY`

```
[slot:u8]
```

Plays the animation in `slot` on the ears. `OK` confirms **the ears accepted the command**, not that
they finished moving — `LIST` carries no duration, so no client can know when playback ends, and no
client should render an in-flight "Playing…" state it has no truthful moment to stop.

`SLOT_EMPTY` and `SLOT_OUT_OF_RANGE` on a `PLAY` mean exactly one thing to a client: **its cached
list is stale.** The correct response is to re-read `LIST` and re-render, not to show an error.

### 7.6 Reassembly, abandonment, recovery

- `chunk_index == 0` **starts a new transfer**, superseding anything in flight — matching what `0x05`
  already does.
- An out-of-order `chunk_index` **discards the transfer** and responds `CHUNK_OUT_OF_ORDER`.
- A `chunk_index > 0` with nothing in flight responds `NO_ACTIVE_TRANSFER`.
- **Any error is terminal for the transfer.** Reassembly state is discarded; a client that keeps
  sending gets `NO_ACTIVE_TRANSFER` per chunk. **Clients stop on the first error.**
- **Disconnect discards all reassembly state.** The connection is exclusive, so nothing can be
  resumed into.
- **Reconnect mid-upload restarts at chunk 0. There is no resume protocol** — free, because `STORE`
  is an unconditional overwrite of an explicitly-named slot.
- **Inter-chunk timeout: 5 s on the device**, then discard **silently**. It exists only to reclaim
  the buffer from a client that vanished without disconnecting; no healthy client approaches it.
  There is deliberately no `TRANSFER_TIMEOUT` status: the client that caused it is usually gone, and
  a merely-slow client discovers the same fact on its next chunk as `NO_ACTIVE_TRANSFER`, which
  carries the same instruction — restart from chunk 0 — without the firmware having to remember
  *why* it has no transfer.

### 7.7 The two reserved opcodes

**`GET_ANIMATION` (read-back) is not in the surface**, and answers `UNSUPPORTED_OPCODE`.

The fact that settles it: the watch has **no authoring UI** and will not get one. Every web-app
upload carries its `Animation.id` and lives authoritatively in the web app's database; the only
device-only animation that ever existed was a hardcoded C++ function, now deleted. Read-back would
rescue data that is not at risk. It is also the most expensive command by far — another genuinely
multi-frame *outbound* transfer over the indication path, roughly doubling the outbound-chunking
surface to serve no user.

**`RENAME` is not in the surface** either. Renaming is `STORE` with the same payload and a different
name — 2 frames on an operation performed rarely — and its absence preserves a strong invariant:
**there is exactly one way to write a slot.** A rename opcode is a second mutation path that must
independently uphold the atomic-overwrite guarantee and carry its own error case. It would not even
be cheaper on flash: the record is one blob with a variable-length name, so a rename is a full
read-modify-write regardless.

**The honest counter-argument, recorded rather than buried:** for a watch-authored slot (zero
`animation_id`) the web app has no payload, and read-back is ruled out — so such a slot's name can
never be changed by anyone. Today that is at most a legacy record on a device flashed before this
change. If watch authoring ever lands you want read-back *and* rename together, which is why both
are reserved values rather than declared impossible.

---

## 8. The capability record and version rules

```
[protocol_version:u8][slot_count:u8][max_chunk_bytes:u16][serial:6]
```

Fetched as a **sub-opcode, not a plain GATT read** — there is no spare characteristic, since `ABF2`'s
value attribute is already occupied by lighting reads.

**Initial value: `protocol_version = 1`.** The serial was appended without bumping it; see the
extensibility rule below.

**Principle: transmit what the client cannot derive from this document and the protocol version;
document everything else.**

| Field | Why it is on the wire |
| --- | --- |
| `max_chunk_bytes` | Varies with the negotiated MTU. Computed as `MTU - 3` (`spp_mtu_size`, `main/ble.c:648`). **Never the 512 API cap** — see §1.4. |
| `slot_count` | Varies with build and partition size. Clients **must not hardcode it**; slot indices are `0..slot_count-1`, and out-of-range gets a distinct status, never a clamp. |
| `serial` | Per-unit identity. Unknowable to any client by construction — Web Bluetooth hides the MAC, and this document cannot state a value that differs per unit. See §8.1. |

Fixed by the protocol version and deliberately **absent** from the wire: `max_keyframes` (64),
`max_name_bytes` (32), and the maximum animation size (769 B). Those are knowable — *this document
states them and the version fixes them* — which is exactly why putting them on the wire would invent
a way for the device to contradict its own spec.

(The principle used to read "transmit what varies at runtime". That was a proxy for knowability which
happened to coincide on the only two fields that then existed. The serial varies neither at runtime
nor by version, and is the first field to separate them.)

**Extensibility rule: clients MUST ignore trailing bytes they do not understand.** Appending a field
is then non-breaking, which is what reserves the version byte for changes that genuinely break.
Additive changes do not bump the version at all — they are absorbed by this rule and by
`UNSUPPORTED_OPCODE`.

**An appended field is fixed-width. Optionality is expressed by a reserved value, never by omitting
the field.** Omission would make the record's length non-monotone — legal lengths of 4, 6, 10, 12 once
anything further is appended — and a client could then no longer locate any field by offset, because
offsets would depend on whether an *earlier optional* field had been emitted. The first optional field
to be omitted rather than reserved is the last optional field the record can ever have. `LIST` already
follows this rule: an all-zero `animation_id` means watch-authored (§7.2).

**Version semantics: `u8`, bumped only on breaking changes, and the client refuses rather than
degrades.** Outside the known range, the client disconnects and tells the user which side is stale.
Degrading requires the client to know what changed in a version it has never seen — it cannot; that
is guessing. Refusing is only unkind when you do not control both ends, and all three repos are
controlled: the web app updates on reload, the ears are flashable.

**Accepted cost, stated plainly:** a firmware bump that touches only the `0x06` surface will still
brick built-in `0x01` playback on an un-updated watch, because a version refusal is a hard
disconnect, not a partial degrade. A half-working connection is exactly the degrading the version
byte forbids.

### 8.1 `serial`

Six bytes, appended last, raw — **not hex on the wire.** ASCII hex would double the bytes and put a
hex encoder in firmware; every client that wants hex is formatting for display and can encode it
itself.

**Derivation:** `SHA-256("milklab-ears-serial-v1" ‖ factory eFuse MAC)`, truncated to the first six
bytes. The MAC is `esp_efuse_mac_get_default` — the factory eFuse MAC, **not** `esp_read_mac` with
`ESP_MAC_BT`, which carries a target-dependent offset and follows `esp_base_mac_addr_set`. Six digest
bytes match the 48-bit width of the input, so the truncation discards no resolution the MAC ever had.

**Presence is a length check, never a version check:**

```
serial present  ⟺  payload.length >= 10  and  bytes 4..9 are not all zero
```

- **All-zero is reserved** and means "this device cannot tell you its serial" — the eFuse read failed.
  It is never a legal serial. Clients must reject it *at the parse boundary*: all-zero renders as the
  perfectly well-formed hex string `000000000000`, so a zero that escapes the parser becomes a serial
  that every failed unit in the fleet shares.
- **4 bytes** is pre-serial firmware.
- **5–9 bytes** is read as no serial. No legal firmware can emit it — the serial's offset and width are
  fixed — so it is a bench bug, and the extensibility rule already covers it: those are trailing bytes
  the client cannot interpret.
- **Under 4 bytes** remains the only length that is rejected outright.

Clients that distinguish the 4-byte and all-zero cases to the user should say different things. The
first is fixed by updating firmware; the second is not, and telling that user to update sends them on
an errand that cannot succeed.

**The derivation is frozen once any client persists a serial.** Changing the domain string, the hash,
or the truncation width makes every existing unit report a *different* serial after a firmware update,
orphaning every record keyed to the old one. The `v1` in the domain string is domain separation, not
an upgrade path — treat it as decorative. If this ever must change, it is a migration for every
consumer, not a firmware bump.

**Why the serial is what it is** — the factory MAC over the BLE MAC, SHA-256 over the alternatives, six
bytes over four or eight, and what the value deliberately does *not* promise — is recorded in
`milk-lab-creations/docs/adr/0002-how-a-pair-of-ears-is-identified.md`. That ADR owns the identity
semantics; this section owns the bytes. The freeze above is stated in both on purpose: the reader most
likely to improve a hash is the one least likely to follow a link out of the repository they are in.

---

## 9. Status codes

```
0x00  OK
0x01  UNSUPPORTED_OPCODE     unknown sub-opcode, or reserved-unimplemented (GET_ANIMATION, RENAME)
0x02  MALFORMED_REQUEST      payload wrong length or shape for this opcode
0x03  SLOT_OUT_OF_RANGE      index >= slot_count
0x04  SLOT_EMPTY             play a slot holding nothing
0x05  INVALID_NAME           >32 bytes, invalid UTF-8, or control chars
0x06  INVALID_ANIMATION      deserialize rejected: bad angle, bad ease type, non-monotonic times,
                             0 or >64 keyframes
0x07  TOO_LARGE              payload exceeds the reassembly buffer
0x08  CHUNK_OUT_OF_ORDER     transfer active, wrong index
0x09  STORAGE_FAILURE        NVS write or erase failed
0x0A  NO_ACTIVE_TRANSFER     chunk_index > 0 with nothing in flight
```

Two absences are deliberate and load-bearing, not oversights: **no `STORE_FULL`** (§7.3 made fullness
a UI state) and **no `TRANSFER_TIMEOUT`** (§7.6).

`CHUNK_OUT_OF_ORDER` stays separate from `NO_ACTIVE_TRANSFER` because "you are out of sync" and
"there is nothing here" are different client situations. `INVALID_NAME` and `INVALID_ANIMATION` stay
separate because the web app acts on them differently — one is a truncation bug, the other a
serializer bug.

### 9.1 An unsubscribed client gets no side effects

**The ears refuse to execute any `0x06` request while `ABF2`'s CCCD is not set for indications.**
Nothing stored, nothing played, nothing deleted; the firmware logs and drops it.

A transactional command whose result cannot be delivered **must not have side effects** — the
alternative is a store that succeeds while the client believes it failed, the worst outcome this
protocol can produce. The client-visible symptom is a request timeout, and since subscribing is step
1 of the mandated connect sequence (§6), a correct client never reaches this.

### 9.2 Client request timeout: 5 s, and it means *unknown*

**A timeout is never a failure.** The ears may well have committed. The recovery is to **re-read
`LIST` and see what actually happened** — never to assume failure, and never to blindly retry, which
could overwrite a slot the user did not choose.

### 9.3 Validation ownership

The ears are authoritative, always. The store path reuses `custom_animation_deserialize`
**verbatim** as its gate before the NVS commit, so store and stream-and-play accept exactly the same
animations by construction. A second validator would create a class of animation that plays but will
not store — a divergence nobody notices until it happens in the field.

**Clients pre-validate for UX only, never for safety.** The web app's `payload.ts` / `limits.ts` exist
so the user sees a problem in the editor instead of a nack from a robot; they are not a trust
boundary. The ears behave identically whether the client validated or not. **There is no rule the
client enforces that the ears do not.**

### 9.4 Names

- **UTF-8**, length counted in **bytes**, `u8` length prefix.
- **1–32 bytes.** Storage is not the constraint; the chunked list response and the watch screen are.
- **Reject** invalid UTF-8 and control characters (`< 0x20`, `0x7F`). **Accept** every other valid
  UTF-8 sequence, emoji included.
- **Duplicates are allowed** — the slot index is the identity, the name is a label.
- **The client truncates; the ears reject.** Over-length is `INVALID_NAME`, never silent truncation.

The ears are storage, not a rendering authority, and the watch is not the only renderer — the web app
draws emoji fine. Pushing a display limitation into the data model would permanently foreclose a
watch-side glyph fallback or a wider LVGL font. Bytes rather than codepoints because the firmware
cannot count codepoints cheaply.

Rejecting rather than truncating is what stops the web app showing "My Extremely Long Animation Name"
while the watch shows "My Extremely Long Anima" — a divergence nobody can see or debug. Rejection
forces the client to truncate *deliberately* at upload time and show the user the 32-byte name it is
actually about to send.

---

## 10. Sizing worked through, in one place

With `max_chunk_bytes = MTU - 3 = 509` and a 5-byte header, **504 payload bytes per frame**:

| Message | Worst case | Frames |
| --- | --- | --- |
| `CAPABILITY` request / response | 5 / 15 bytes | 1 / 1 |
| `LIST` response | `1 + 16 * (1 + 16 + 1 + 32)` = **801 B** | **2** |
| `STORE` request | `1 + 16 + 1 + 32 + (1 + 64 * 12)` = **819 B** | **2** |
| `DELETE` / `PLAY` request | 1 byte payload | 1 |

Two consequences the firmware must act on:

- **Chunking is mandatory on the read path**, not merely defensive. At 16 slots with 32-byte names a
  full listing does not fit one frame.
- **`CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE` is 769 B, smaller than an 819-byte store request.** The
  ears need a **separate 819-byte store reassembly buffer**; the existing `0x05` buffer cannot be
  reused as-is. Keeping them separate also avoids coupling the store path to the stream-and-play
  path. `TOO_LARGE` is the status when a transfer would exceed it.

Both figures scale with `slot_count` and the 32-byte name cap. Raising either re-opens this table.

---

## 11. What is new code

§4–§10 is now implemented, apart from the two reserved opcodes of §7.7. This section is kept as
written, in the present tense of the firmware it was written against: these were the items most
easily mistaken for "already there" or "just a tweak".

### 11.1 `ABF1` is missing the WRITE property bit

`ABF1` declares `char_prop_read_write`, which is `WRITE_NR | READ` — no `WRITE` bit
(`main/ble.c:155`). Write-with-response nonetheless works today, because the value attribute carries
`ESP_GATT_PERM_WRITE` and Bluedroid enforces permissions rather than declared properties.

**Add the bit anyway.** The Web Bluetooth spec says `writeValueWithResponse` should reject with
`NotSupportedError` when the `write` property is absent; Chrome resolving here is undefined behaviour
we should not build a protocol on.

### 11.2 `ABF2` must declare INDICATE, and the whole indication path is new

Two separate changes:

- **Property.** Ensure the build selects `ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_INDICATE`
  at `main/ble.c:159` — i.e. **`CONFIG_EXAMPLE_SPP_THROUGHPUT` off**, or the `#ifdef` removed
  outright. The generated `sdkconfig` currently has it **on** (`sdkconfig:500`), which selects
  NOTIFY. This is a build-config change with a wire-visible effect, so it must not be left to
  whatever a stale `sdkconfig` happens to say.
- **The CCCD write handler is new firmware.** `enable_data_ntf` (`main/ble.c:79`) is initialised
  `false`, tested at `main/ble.c:367`, reset to `false` on disconnect at `main/ble.c:708`, and
  **never assigned `true` anywhere**. The handler that would set it was lost when the write path was
  rerouted through `controller_handle_write`, which dispatches only on the `ABF1` value handle and
  drops CCCD writes. Until it exists, **no client action can produce anything on `ABF2`**.

Also new: an outbound path at all. The only `esp_ble_gatts_send_indicate` calls on `ABF2`
(`main/ble.c:425,434`) are fed by the **UART0 driver queue** inherited from the Espressif SPP
example. `controller_update_lighting_characteristic` and its siblings only call
`esp_ble_gatts_set_attr_value` — they update the *readable* value and send nothing. Outbound framing,
chunking and indication-confirmation sequencing (§5) are all to be written.

### 11.3 Name validation has never existed

The existing validator has never seen a name. UTF-8 well-formedness, the 1–32 byte bound and the
control-character rejection (§9.4) are all new code.

### 11.4 The exact-length check on `STORE`

`custom_animation_deserialize` checks `data_len <` (not `!=`), so it silently tolerates surplus
bytes. That is fine for a fire-and-forget play; it is **not** fine when persisting a blob, where a
length mismatch means client and ears disagree about what was sent and the surplus becomes permanent
flash content. §7.3 step 4 is an added check, not the existing one.

### 11.5 The `anim` partition

A custom partition CSV, `nvs_flash_init_partition("anim")`, and the record layout of §3.3. There is
no NVS code for animations today.

### 11.6 A separate 819-byte reassembly buffer

Distinct from `custom_animation_buffer` (769 B) in `main/controller.c`. See §10.

### 11.7 Two-level dispatch

`data_packet_unpack` currently rejects any type byte outside `0x01`–`0x05`
(`main/types/ble_packet_types.h`). `0x06` must be accepted, reassembled generically at the transport
layer (§5), and dispatched on its sub-opcode only once complete.

### 11.8 The device serial

Nothing in `main/` reads eFuse or hashes anything today. `respond_capability` (`main/store.c:92`)
gains the §8.1 derivation, six bytes longer.

Three things that are not obvious from the record layout:

- **`mbedtls` must be added to `main`'s `PRIV_REQUIRES`.** It links today only transitively via `bt`,
  and `MINIMAL_BUILD ON` makes that an accident rather than a guarantee. Relying on it is how this
  becomes a link error on someone else's build.
- **The read is cheap and needs no init.** `esp_efuse_mac_get_default` bottoms out in a memory-mapped
  register read — no flash, no NVS, no lock, no radio — and is correct before `esp_bt_controller_init`.
  `mbedtls_sha256_init` is a `memset`. `CAPABILITY` can answer without pre-computing anything, though
  computing the serial once at boot is equally fine.
- **Handle the error rather than assuming success.** On a failed read, emit six zero bytes (§8.1) —
  never a partial record, and never a made-up value.

---

## 12. Client flows

### 12.1 Connect

1. `requestDevice` (web) or reconnect to the last address (watch), then `connect()`.
2. Discover `0xABF0`, get `ABF1` and `ABF2`.
3. **Subscribe to `ABF2`.**
4. `CAPABILITY`. If `protocol_version` is outside the known range, **disconnect and say which side is
   stale** (§8). Do not proceed.
5. `LIST`. Cache the entries, `slot_count` and `max_chunk_bytes` for the connection, **tagged with the
   client's own stable identifier for the device** (§6) — not the serial, which is optional.

Every GATT operation is serialized **app-wide** — one in flight at a time across the whole client,
not per characteristic. Concurrent Web Bluetooth operations may reject with `NetworkError`; Blink has
no operation queue, and the spec's own note says sites must serialize.

### 12.2 Store an animation

1. Client-side pre-flight for UX only: `<= 64` keyframes, angles `<= 180`, ease types `<= 3`,
   non-decreasing times, name truncated to 32 **bytes** without splitting a code point.
2. Build the `STORE` payload (§7.3). Split into `ceil(len / (max_chunk_bytes - 5))` frames.
3. Write each frame to `ABF1` **with response**, in order, `chunk_index` ascending, the same `corr`
   throughout.
4. Await the indication on `ABF2`.
   - `OK` — it is in flash. Update the cached list entry for that slot.
   - Any other status — **stop sending**; the transfer is dead on the device (§7.6). Map the status
     to a sentence saying what to do next.
   - No response within 5 s — **unknown outcome**, go to §12.4.

### 12.3 Delete / play

Single frame, single indication. On `PLAY`, treat `SLOT_EMPTY` and `SLOT_OUT_OF_RANGE` as *stale
cache*, not error: re-read `LIST`, re-render, and say the animation is no longer there rather than
that something failed (§7.5).

### 12.4 Recovery from a timeout or a dropped connection

1. Say the outcome is being checked, not that it failed.
2. Reconnect if the link dropped, re-running §12.1 in full — including `CAPABILITY`, since a
   reconnected link may negotiate a different MTU.
3. Re-read `LIST` and compare against what was attempted.
4. Report what **actually** happened. Never assume failure; never silently retry a `STORE`, which
   could overwrite a slot the user did not choose.

### 12.5 Client-specific behaviour

Neither of these is part of the wire contract; they are recorded here so an implementer of one repo
can see what the other assumes.

- **The watch is play-only.** It implements the connect sequence, `LIST` and `PLAY`, and nothing
  else — no `STORE`, no `DELETE`. It caches the list in RAM tagged with its own device handle (§6),
  fetches once on connect rather than on every screen entry, dims rather than clears its buttons on
  disconnect, and discards the cache outright when connecting to a *different* device. It has no use
  for the serial and ignores it: `onCapabilityResponse` guards on `_rx_length < 4` — a minimum, not an
  equality — so the longer record needs no watch change at all. The full screen design is on the
  *Decide: watch animate screen as a pure client* card.
- **The web app is the manager.** Connecting is a global header chip; uploading is a dialog showing
  all `slot_count` real slots. It implements `STORE`, `DELETE` and `PLAY`, defaults the target to the
  slot already holding this `animation_id` (else the first free one), confirms overwrites inline, and
  makes the device-side name editable at upload because the web name allows 100 characters against
  the device's 32 bytes. Full rationale in
  `milk-lab-creations/docs/adr/0001-web-app-connect-and-upload-ux.md`.

**The ears own the store, the web app manages it, the watch plays from it.**

---

## 13. Deliberately not covered

Ruled out of scope for this contract, recorded so nobody re-litigates them by accident:

- **Redesigning the one-byte-type multiplexing.** Adding the WRITE bit to `ABF1` is not a redesign.
- **Concurrent multi-client connections and live change notification.**
- **iOS support and a native phone app.** All iOS browsers use WKWebView, which has no Web Bluetooth.
- **Retiring the built-in `0x01` animations.** A firmware intention, tracked on the Robo Cat Ears
  board; the watch grid is already designed to survive it.
- **Syncing device state back to the web app's database.**
- **The esp32c3-vs-s3 target question.** Both sdkconfigs declare a 2 MB flash and the layout ends at
  `0x120000`, so it fits either way: it gates build configuration, not this contract.

Known-open, and expected to be answered outside this document:

- ~~**Cross-repo wire-format conformance testing.**~~ Answered: a shared golden fixture, canonical
  at `docs/spec/wire-format-fixture.json` in the web repo and copied here to
  `test/wire-format-fixture.json`. `make -C test` diffs the copy against the canonical and
  runs `test/wire_format_conformance.c` over it; the web app runs its own test over the same bytes.
  See §2.6.
- **How the watch renders names using glyphs Montserrat lacks** — fallback character, omission, or a
  wider LVGL font. §9.4 deliberately left this to the renderer rather than the wire format.
- **Whether the web editor's "try it on the real ears" preview is actually built.** `0x05` was kept
  alive (§2.5) specifically to serve it.
