# NVS as the store of record for custom animations

Research notes for moving user-created custom animations from "streamed over BLE, played from
RAM" to "owned by the firmware, persisted in NVS".

**Everything below is sourced from primary material only:**

- ESP-IDF **v5.5.2** (tag `v5.5.2`, commit `30aaf64524299d3bde422ca9a2848090d1bc5d0f`), installed
  locally at `/home/jeffu/esp/v5.5.2/esp-idf`. This is the version this project actually builds
  against (`dependencies.lock` line 14: `version: 5.5.2`).
- Paths written as `components/nvs_flash/...` are relative to that install, and correspond to
  <https://github.com/espressif/esp-idf/blob/v5.5.2/components/nvs_flash/...>.
- Rendered docs: <https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32c3/api-reference/storage/nvs_flash.html>
  and <https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32c3/api-guides/partition-tables.html>,
  whose sources are `docs/en/api-reference/storage/nvs_flash.rst` and
  `docs/en/api-guides/partition-tables.rst` in the same tree.

No blog posts, Stack Overflow answers, or forum threads were used.

---

## 0. Verification of the starting facts

| Claim under test | Verdict | Evidence |
| --- | --- | --- |
| Stock single-app partition table: `nvs` @ `0x9000` / `0x6000`, `phy_init` @ `0xf000` / `0x1000`, `factory` @ `0x10000` / `0x100000` | **Confirmed** | Decoded `build/partition_table/partition-table.bin` directly. Entries: `nvs type=0x01 sub=0x02 off=0x9000 size=0x6000`, `phy_init type=0x01 sub=0x01 off=0xf000 size=0x1000`, `factory type=0x00 sub=0x00 off=0x10000 size=0x100000`. Matches `components/partition_table/partitions_singleapp.csv` and the table printed in `docs/en/api-guides/partition-tables.rst` lines 25–36. |
| `CONFIG_ESPTOOLPY_FLASHSIZE_2MB` is set, ~1 MB unallocated | **Confirmed, with a caveat** | `sdkconfig` has `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y` / `CONFIG_ESPTOOLPY_FLASHSIZE="2MB"`. Highest allocated address is `0x10000 + 0x100000 = 0x110000` = 1 114 112 B, leaving 2 097 152 − 1 114 112 = **983 040 B (960 KB)** unallocated. Caveat: 2 MB is simply the ESP-IDF Kconfig **default** (`components/esptool_py/Kconfig.projbuild:112-121`, `default ESPTOOLPY_FLASHSIZE_2MB`), so it is evidence of *nothing having been configured*, not of the hardware. See §6. |
| `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"` is set but the file does not exist, and `SINGLE_APP` is what takes effect | **Confirmed** | `sdkconfig` has `CONFIG_PARTITION_TABLE_SINGLE_APP=y`, `# CONFIG_PARTITION_TABLE_CUSTOM is not set`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`, and — decisively — `CONFIG_PARTITION_TABLE_FILENAME="partitions_singleapp.csv"`. `components/partition_table/Kconfig.projbuild:129-151` shows `PARTITION_TABLE_CUSTOM_FILENAME` has `default "partitions.csv"` unconditionally and is only *prompted* when `PARTITION_TABLE_CUSTOM` is set; `PARTITION_TABLE_FILENAME` (the value the build actually consumes) only falls through to it `if PARTITION_TABLE_CUSTOM` (line 151). So the stale filename is inert. No `partitions.csv` exists at the repo root. |
| Existing namespaces `lighting`, `servo_cal`, `anim_mode` | **Confirmed** | `main/led.c:22` (`#define NVS_NAMESPACE "lighting"`, key `"config"`), `main/servo_calibration.h:16-17` (`"servo_cal"` / `"calibration"`), `main/animation_mode.h:21-22` (`"anim_mode"` / `"mode"`). All three are written with `nvs_set_blob` (`main/led.c:119`, `main/servo_calibration.c:199`, `main/animation_mode.c:123`). |
| ~800 bytes worst case per animation | **Confirmed** | `main/types/custom_animation_types.h:33`: `CUSTOM_ANIMATION_MAX_SERIALIZED_SIZE = (1 + CUSTOM_ANIMATION_MAX_KEYFRAMES * CUSTOM_ANIMATION_KEYFRAME_SIZE)` = `1 + 64 * 12` = **769 bytes**. Plus a 16-byte UUID and a short name → ~800–810 bytes. |

**One fact the starting list missed, and it matters:** the `nvs` partition is not exclusively the
application's. `sdkconfig` sets `CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=y`, and Bluedroid persists its
bonding database into the *same default* `nvs` partition — `components/bt/common/osi/config.c:85`
and `:422` call `nvs_open(filename, NVS_READWRITE, ...)`, where `filename` is
`"bt_config.conf"` (`components/bt/host/bluedroid/btc/core/btc_config.c:25`), and the config is
stored as a chain of blobs keyed `bt_cfg_key0`, `bt_cfg_key1`, … (`config.c:25`, `:372`, `:480`,
`:491`). Its size grows with the number of bonded peers. This is budgeted for in §7.

---

## 1. Real usable capacity of the 24 KB `nvs` partition

### 1.1 The constants

All in `components/nvs_flash/private_include/nvs_constants.h`:

| Constant | Value | Line |
| --- | --- | --- |
| `NVS_CONST_PAGE_SIZE` | `SPI_FLASH_SEC_SIZE` | 21 |
| `NVS_CONST_ENTRY_SIZE` | **32** bytes | 23 |
| `NVS_CONST_ENTRY_COUNT` | **126** entries per page | 24 |
| `NVS_CONST_CHUNK_MAX_SIZE` | `32 * (126 - 1)` = **4000** bytes | 27 |
| `NVS_CONST_PAGE_HEADER_OFFSET` | 0 | 64 |
| `NVS_CONST_PAGE_ENTRY_TABLE_OFFSET` | header + **32** | 65 |
| `NVS_CONST_PAGE_ENTRY_DATA_OFFSET` | entry table + **32** | 66 |

The sector size is hard-coded to 4096 for the page abstraction:
`components/nvs_flash/src/nvs_page.cpp:18` — `const uint32_t nvs::Page::SEC_SIZE = 4096;`
(asserted against the real flash sector size at init, `src/nvs_api.cpp:132` and `:173`).

So the page layout is exactly:

```
4096 = 32 (page header) + 32 (entry state bitmap) + 126 x 32 (4032 bytes of entries)
```

which is the diagram in `docs/en/api-reference/storage/nvs_flash.rst:230-251`. The bitmap is
2 bits per entry; `126 x 2 = 252` bits used of 256, "the final four bits in the bitmap
(256 - 2 * 126) are not used" (`nvs_flash.rst:264`).

### 1.2 The reserved page

NVS refuses to operate read-write without at least one completely free page.
`components/nvs_flash/src/nvs_pagemanager.cpp:129-133`:

```cpp
// partition should have at least one free page if it is not read-only
if (mFreePageList.empty()) {
    return ESP_ERR_NVS_NO_FREE_PAGES;
}
```

The same reservation is what the public statistics API reports.
`nvs_pagemanager.cpp:236-239`:

```cpp
nvsStats.total_entries += mFreePageList.size() * Page::ENTRY_COUNT;
nvsStats.free_entries  += mFreePageList.size() * Page::ENTRY_COUNT;
// calculate available entries from free entries by applying reserved page size
nvsStats.available_entries = (nvsStats.free_entries >= Page::ENTRY_COUNT) ? nvsStats.free_entries - Page::ENTRY_COUNT : 0;
```

i.e. `available_entries = free_entries - 126` — one full page is permanently subtracted. The docs
state the same rule as a minimum size: "The default minimal size for NVS to function properly is
12kiB (`0x3000`), meaning there have to be at least 3 pages with one of them being in Empty state"
(`nvs_flash.rst:377`).

### 1.3 The arithmetic

```
partition          0x6000  = 24 576 bytes
pages              24 576 / 4096            =   6 pages
reserved (free)                             = - 1 page
usable pages                                =   5 pages
usable entries     5 x 126                  = 630 entries
usable entry bytes 630 x 32                 = 20 160 bytes
```

**The ~20 KB figure on the design map is correct** — 20 160 bytes, to be exact — *but it is the
gross entry pool, not bytes of user payload.* Every item consumes at least one entry of pure
header before any payload lands (`src/nvs_page.cpp:186-192`):

```cpp
size_t totalSize = ENTRY_SIZE;
size_t entriesCount = 1;
if (isVariableLengthType(datatype)) {
    size_t roundedSize = (dataSize + ENTRY_SIZE - 1) & ~(ENTRY_SIZE - 1);
    totalSize += roundedSize;
    entriesCount += roundedSize / ENTRY_SIZE;
}
```

The 32-byte entry header layout (namespace index, datatype, span, chunk index, CRC32, 16-byte key,
8-byte value/metadata union) is `src/nvs_types.hpp:43-67` and documented at
`nvs_flash.rst:281-344`. Key names are capped at 15 characters + NUL
(`components/nvs_flash/include/nvs.h:61`, `NVS_KEY_NAME_MAX_SIZE 16`); namespace names likewise
(`nvs.h:62`).

Namespaces themselves cost an entry each: a namespace is stored as a `U8` item in namespace
index 0 (`src/nvs_storage.cpp:626`, `writeItem(Page::NS_INDEX, ItemType::U8, nsName, &ns, sizeof(ns))`;
described at `nvs_flash.rst:346-360`). Max 254 namespaces (`nvs_flash.rst` Namespaces section).

**Corrected capacity statement:** 630 entries / 20 160 bytes of entry pool, of which user payload
is `entry_pool − (2 entries per blob) − (1 entry per namespace) − rounding-to-32 waste`.

---

## 2. Blob mechanics

### 2.1 Every `nvs_set_blob` goes through the multi-chunk path

There is no "small blob" fast path. `Storage::writeItem` dispatches all `ItemType::BLOB` writes to
`writeMultiPageBlob` unconditionally (`src/nvs_storage.cpp:468-497`, the call is at line 491), even
for a 4-byte blob. Consequence: a blob is *always* stored as ≥1 `BLOB_DATA` chunk plus one
`BLOB_IDX` index entry.

The type enum making this concrete (`components/nvs_flash/include/nvs_handle.hpp:27-41`):

```cpp
SZ   = NVS_TYPE_STR,
BLOB = 0x41,
BLOB_DATA = NVS_TYPE_BLOB,
BLOB_IDX  = 0x48,
```

### 2.2 Chunking

`Storage::writeMultiPageBlob` (`src/nvs_storage.cpp:273-370`):

1. Compute the ceiling: `max_pages = mPageManager.getPageCount() - 1`, further clamped to
   `(Page::CHUNK_ANY - 1) / 2 = 127` (lines 281-284). If
   `dataSize > max_pages * Page::CHUNK_MAX_SIZE`, return `ESP_ERR_NVS_VALUE_TOO_LONG` (lines 288-290).
2. Loop: take the current page's `getVarDataTailroom()`, write
   `chunkSize = min(remainingSize, tailroom)` as an `ItemType::BLOB_DATA` item with
   `chunkIndex = chunkStart + chunkCount` (lines 318-325), mark the page FULL and request a new one
   when data remains (lines 336-348).
3. When done, write a single `BLOB_IDX` item carrying `dataSize`, `chunkCount`, `chunkStart`
   (lines 350-360).

`Page::getVarDataTailroom()` (`src/nvs_page.cpp:1093-1102`) reserves exactly one entry for the
chunk's own header:

```cpp
/* Skip one entry for blob data item processing the data */
return ((mNextFreeEntry < (ENTRY_COUNT - 1)) ? ((ENTRY_COUNT - mNextFreeEntry - 1) * ENTRY_SIZE) : 0);
```

so a chunk of `tailroom` bytes fits a page exactly: `1 header entry + tailroom/32 data entries`.

### 2.3 The `VER` scheme

`src/nvs_types.hpp:21-31`:

```cpp
/**
 * Used to recognize transient states of a blob. Once a blob is modified, new chunks with the new
 * data are written with a new version. The version is saved in the highest bit of Item::chunkIndex
 * as well as in Item::blobIndex::chunkStart.
 * If a chunk is modified and hence re-written, the version swaps: 0x0 -> 0x80 or 0x80 -> 0x0.
 */
enum class VerOffset: uint8_t { VER_0_OFFSET = 0x0, VER_1_OFFSET = 0x80, VER_ANY = 0xff };
```

On overwrite, `Storage::writeItem` reads the previous `BLOB_IDX`, toggles `chunkStart` to the other
version, and writes the **entire new blob under the opposite version tag before deleting anything**
(`src/nvs_storage.cpp:484-491`). This is the crash-safety mechanism and it is also the source of
the 2× peak-space requirement (§3).

Because chunk indices are `chunkStart + n` with `chunkStart ∈ {0x00, 0x80}`, a blob can have at most
128 chunks per version — hence the `(CHUNK_ANY-1)/2` clamp above.

### 2.4 Maximum single blob on a 24 KB partition

```
max_pages       = 6 - 1                    = 5
CHUNK_MAX_SIZE  = 32 x (126 - 1)           = 4 000 bytes
max blob        = 5 x 4 000                = 20 000 bytes
```

Anything larger returns `ESP_ERR_NVS_VALUE_TOO_LONG` (`src/nvs_storage.cpp:288-290`). The
documented rule of thumb agrees: "Blob values are limited to 508,000 bytes or 97.6% of the
partition size - 4000 bytes, whichever is lower" (`nvs_flash.rst:342`) →
`0.976 x 24 576 − 4 000 = 19 986`, which is the same number to within the doc's rounding.

For comparison, strings are capped at **4000 bytes** and must fit contiguously *in one page*
(`nvs_flash.rst:342`, `nvs.h:277-295`, and `nvs_constants.h:28`
`NVS_CONST_STR_LEN_MAX_SIZE = 32 * 125`). Strings do not get an index entry and cannot span pages
(`nvs_flash.rst:281`: "For strings, in case when a key-value pair spans multiple entries, all
entries are stored in the same page").

### 2.5 Overhead, exactly

The public header states the cost model outright (`components/nvs_flash/include/nvs.h:317-321`):

> Sets variable length binary value for the key. Function uses **2 overhead and 1 entry per each
> 32 bytes of new data** from the pool of available entries. […] In case of value update for
> existing key, space occupied by the existing value and 2 overhead entries are returned to the
> pool of available entries.

So:

```
entries(blob of N bytes) = 2 + ceil(N / 32)
bytes                    = 64 + 32 * ceil(N / 32)
```

The 2 overhead entries are the `BLOB_DATA` chunk header and the `BLOB_IDX` entry. Each *additional*
chunk (i.e. each page boundary crossed) costs one more header entry.

For the animation record:

| Layout | Entries | Bytes | Overhead |
| --- | --- | --- | --- |
| One 805-byte blob (name + UUID + keyframes packed) | `2 + ceil(805/32)` = `2 + 26` = **28** | 896 | 91 B (11.3%) |
| Split: blob(769) + str(name, 24) + blob(uuid, 16) | `2+25` + `1+1` + `2+1` = **32** | 1024 | 219 B (27%) |

**One blob per animation is meaningfully cheaper** — 4 entries (128 bytes) per slot, ~14% of the
slot budget, and it also halves the number of `findItem` lookups on read (§5). It additionally
avoids the three-keys-must-stay-consistent problem: a single blob write is atomic-by-version
(§2.3), three separate keys are not.

A ~800-byte blob is stored efficiently: it fits inside a single page's chunk limit
(805 < 4000), so it is normally one chunk + one index = 28 entries with only ~27 bytes lost to
32-byte rounding.

---

## 3. Rewrite cost, compaction, and running near full

### 3.1 What an overwrite does

For blobs (`src/nvs_storage.cpp:468-527`), in order:

1. `findItem(..., BLOB_IDX, ...)` locates the old index entry (line 406).
2. `cmpMultiPageBlob` — **if the new value is byte-identical, the write is skipped entirely** and
   `ESP_OK` returns without touching flash (lines 476-481). Idempotent re-saves are free.
3. Otherwise the version is toggled and the *whole new blob is written* (line 491).
4. **Only then** is the old value deleted (the `// Delete previous value` block, from line 534).

Deletion is not an erase. Entries are marked `ERASED` in the 2-bit entry state bitmap
(`nvs_page.hpp:34-35` `ESB_WRITTEN`/`ESB_ERASED`; `nvs_flash.rst:264-273`), which is a write of
zero bits — no sector erase needed. The docs summarise this design:

> Set operations are appending new data to the free space after existing entries. Invalidation of
> old values doesn't require immediate flash erase operations. The organization of NVS space to
> pages and entries effectively reduces the frequency of flash erase to flash write operations by a
> factor of 126.
> — `nvs_flash.rst:22`

### 3.2 When compaction runs

Only inside `PageManager::requestNewPage()` (`src/nvs_pagemanager.cpp:139-199`), and only when the
free list has dropped to exactly one page:

```cpp
if (mFreePageList.empty())      return ESP_ERR_NVS_INVALID_STATE;
if (mFreePageList.size() >= 2)  return activatePage();   // no GC needed

// find the page with the highest number of erased items
... for each page: unused = Page::ENTRY_COUNT - it->getUsedEntryCount(); keep the max ...
if (maxUnusedItems == 0) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;

activatePage();                 // consume the last free page
erasedPage->markFreeing();
erasedPage->copyItems(*newPage);
erasedPage->erase();
mFreePageList.push_back(erasedPage);
```

Three properties follow directly, and they are the crux of the rewrite question:

- GC is **lazy** — it never runs until the partition is down to its last free page.
- GC reclaims **exactly one page per call**, the single page with the most unused entries. It does
  not consolidate the whole partition.
- GC **copies the surviving items of that page into the newly activated page**. If the victim page
  still holds more live entries than the fresh page can take, the copy is what consumes the
  headroom.

### 3.3 Yes — a nearly-full partition can fail a write that "fits"

Confirmed on three independent mechanisms, all in primary source:

**(a) The 2× peak during blob update.** Because the new version is written in full before the old
is released (§3.1 steps 3–4), updating an existing 805-byte animation transiently needs **28 free
entries in addition to the 28 the old copy still occupies** — 56 entries / 1792 bytes for a record
that "only" takes 896 bytes at rest. `nvs.h:319-320` says the old space is returned to the pool
only "in case of value update", i.e. afterwards.

**(b) Fragmentation abandoning page tailroom.** `writeMultiPageBlob` lines 296-311: when this is the
first chunk, the current page's tailroom is smaller than the blob, *and* tailroom is below
`CHUNK_MAX_SIZE/10` = **400 bytes**, the page is marked FULL and skipped — that tailroom (up to 399
bytes, ~12 entries) is stranded until the page is garbage-collected. If the newly requested page
has the same tailroom, the call returns `ESP_ERR_NVS_NOT_ENOUGH_SPACE` (lines 307-309) even though
`nvs_get_stats().available_entries` may be non-zero.

**(c) `maxUnusedItems == 0`.** `nvs_pagemanager.cpp:162-164` returns
`ESP_ERR_NVS_NOT_ENOUGH_SPACE` when the last free page is reached and no page has a single
reclaimable entry.

Espressif documents the general hazard for strings explicitly (`nvs.h:285-286`):

> Note that storage of long string values can fail due to fragmentation of nvs pages even if
> `available_entries` returned by `nvs_get_stats` suggests enough overall space available.

and the preconditions per type (`nvs_flash.rst:62`):

> Before setting new or updating existing key-value pair, free entries in nvs pages have to be
> available. For integer types, at least one free entry has to be available. For the String value,
> at least one page capable of keeping the whole string in a contiguous row of free entries has to
> be available. For the Blob value, the size of new data has to be available in free entries.

**Quantified headroom rule.** ESP-IDF does not publish a single "keep N% free" number; the
enforceable figures it *does* give are:

- **126 entries (one page, 4096 B) permanently reserved** — subtracted from `available_entries` at
  `nvs_pagemanager.cpp:236-239`.
- **`2 + ceil(N/32)` entries free** for the new copy of the largest blob you will ever rewrite,
  while the old copy is still live (`nvs.h:317-318`).
- **Up to 399 bytes (12 entries) per page** may be stranded by rule (b) above.

So the defensible working rule for this project is:

```
safe headroom = 126 (reserved page)
              + 28  (a full new copy of the largest animation)
              + 12  (worst-case stranded tailroom on the active page)
              = 166 entries ~ 5.3 KB
```

held free at all times, on top of steady-state usage. I could not find any Espressif statement of a
percentage-based fullness threshold; treat the 166-entry figure as *derived from the code above*,
not as a quoted number.

---

## 4. Partition options

### 4.1 Growing `nvs` in place — blocked by the layout

`nvs` occupies `0x9000..0xf000`. The very next byte is `phy_init` (`0xf000`), and immediately after
that the app at `0x10000`. **There is no room to extend `nvs` upward without relocating both
`phy_init` and `factory`.** The ~960 KB of unallocated flash is at the *top* (`0x110000..0x200000`),
not adjacent to `nvs`.

### 4.2 The mechanics of a custom table

Per `docs/en/api-guides/partition-tables.rst:59-70` and
`components/partition_table/Kconfig.projbuild:61-67, 129-151`:

1. Set `CONFIG_PARTITION_TABLE_CUSTOM=y` (menuconfig → Partition Table → "Custom partition table
   CSV"). This is the switch currently *not* set in `sdkconfig`.
2. `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` already reads `"partitions.csv"` (its Kconfig default,
   line 131) — so the file just needs to exist at the project root. Once `PARTITION_TABLE_CUSTOM`
   is set, `PARTITION_TABLE_FILENAME` picks it up (line 151).
3. Write the CSV. Subtype `nvs` is `data, nvs` (`partition-tables.rst:161, 168`).

Espressif's sizing advice for exactly this situation (`partition-tables.rst:177-179`):

> - It is strongly recommended that you include an NVS partition of at least 0x3000 bytes in your project.
> - **If using NVS API to store a lot of data, increase the NVS partition size from the default 0x6000 bytes.**

Multiple NVS partitions are a first-class feature: `nvs_flash_init_partition(const char*)`
(`include/nvs_flash.h:93`), `nvs_flash_erase_partition(const char*)` (`:165`), and
`nvs_open_from_partition(part_name, namespace, mode, &handle)` (`include/nvs.h:195`).

### 4.3 Does changing the partition table destroy existing NVS data?

**No — flashing a new partition table does not erase anything by itself.** Stated directly at
`docs/en/api-guides/partition-tables.rst:319`:

> Note that updating the partition table does not erase data that may have been stored according to
> the old partition table. You can use `idf.py erase-flash` (or `esptool.py erase_flash`) to erase
> the entire flash contents.

What *does* invalidate NVS is a change to the `nvs` partition's **offset**, because the library
then reads a different region of flash. Two failure shapes:

- The new region is erased/blank → NVS formats itself silently on first init. Data lost, no error.
- The new region holds unrelated old bytes → `Page::load` fails header/CRC validation and the page
  goes to `CORRUPT` state (`nvs_flash.rst:207`), or the version byte mismatches and
  `ESP_ERR_NVS_NEW_VERSION_FOUND` is returned (`src/nvs_page.cpp:75`). If no free page can be
  found, init returns `ESP_ERR_NVS_NO_FREE_PAGES` (`src/nvs_pagemanager.cpp:132`,
  `include/nvs_flash.h:69`). The standard recovery idiom is erase-and-retry, e.g.
  `src/nvs_api.cpp:154`.

**Changing only the *size* of a partition, keeping its offset, is non-destructive to the bytes
already there** — NVS scans `sectorCount` sectors from `baseSector` and puts every sector whose
header it cannot parse onto the free list (`PageManager::load`, `src/nvs_pagemanager.cpp:26-44`).
Newly added blank sectors simply become free pages. But per §4.1 that option is unavailable here
without moving `phy_init` and `factory`, and moving `factory` changes the app offset (a full
reflash, which is fine, but the bootloader and app must agree — `Kconfig.projbuild:159`).

### 4.4 The field-upgrade story

For a device already in the field, the safe route is **adding a second NVS partition in the
unallocated tail, leaving `nvs` @ `0x9000` / `0x6000` byte-for-byte untouched**:

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x100000,
anim,     data, nvs,     0x110000, 0x10000,
```

Properties of this layout:

- `nvs`, `phy_init` and `factory` keep identical offsets and sizes → existing lighting, servo
  calibration, animation-mode settings and BLE bonds **survive the upgrade**, guaranteed by
  `partition-tables.rst:319`.
- `anim` lands in flash that the stock table never allocated. On a factory-fresh chip it is `0xff`
  (blank). On a device that has been flashed with other images before, it may hold stale bytes.
  Firmware should therefore treat `ESP_ERR_NVS_NO_FREE_PAGES` / `ESP_ERR_NVS_NEW_VERSION_FOUND`
  from `nvs_flash_init_partition("anim")` as "erase and retry" via `nvs_flash_erase_partition`
  (the idiom at `src/nvs_api.cpp:154`).
- 64 KB (`0x10000`) = 16 pages = 15 usable = 1890 entries = 60 480 bytes of entry pool, which is
  ~67 slots of 28 entries. Room to grow without another partition change.
- Cost: an ordinary `idf.py flash` (bootloader + partition table + app), no special migration step.

The only thing this **cannot** survive is `idf.py erase-flash` or `esptool.py erase_flash`, which
wipes everything including `nvs`.

**Unestablished:** whether the deployed devices' flash actually extends to 2 MB and whether the
region above `0x110000` is genuinely free on already-shipped units. Nothing in this repo records
what has previously been written to those addresses. This must be verified on real hardware before
committing to the `0x110000` offset.

---

## 5. Read performance: enumerating N blobs at connect

### 5.1 The iterator API exists and is usable, with one important quirk

`nvs_entry_find(part_name, namespace_name, type, &iterator)` /
`nvs_entry_next(&it)` / `nvs_entry_info(it, &info)` / `nvs_release_iterator(it)` —
`nvs_flash.rst:80-89`, implementations at `src/nvs_api.cpp:774`, `:851`, `:869`.

The quirk: the iterator does **not** yield `BLOB_IDX` or the legacy `BLOB` type
(`src/nvs_storage.cpp:1086-1091`):

```cpp
inline bool isIterableItem(Item& item)
{
    return (item.nsIndex != 0 && item.datatype != ItemType::BLOB && item.datatype != ItemType::BLOB_IDX);
}
inline bool isMultipageBlob(Item& item)
{
    return (item.datatype == ItemType::BLOB_DATA &&
            !(item.chunkIndex == static_cast<uint8_t>(VerOffset::VER_0_OFFSET)
                    || item.chunkIndex == static_cast<uint8_t>(VerOffset::VER_1_OFFSET)));
}
```

It yields the **first `BLOB_DATA` chunk** of each blob (chunk index 0x00 or 0x80), exactly once per
blob, and reports `info.type = item.datatype` (`Storage::fillEntryInfo`, `src/nvs_storage.cpp:1048-1060`).
Since `BLOB_DATA = NVS_TYPE_BLOB` (`include/nvs_handle.hpp:38`), passing `NVS_TYPE_BLOB` to
`nvs_entry_find` does the right thing and enumerates each blob once. `nvs_entry_info_t` gives you
namespace name, key name, and type — **not the value or its size**; you still need
`nvs_get_blob(handle, key, NULL, &len)` per key to size it.

Also note: enumeration is *not* free of the namespace filter cost —
`Storage::findEntry` (`src/nvs_storage.cpp:1062-1075`) resolves the namespace name to an index once,
then walks.

### 5.2 The cost profile

`Storage::nextEntry` (`src/nvs_storage.cpp:1100-1120`) is a linear walk over pages and, within each
page, over entries via `Page::findItem(..., key = nullptr, ...)`:

```cpp
for(auto page = it->page; page != mPageManager.end(); ++page) {
    do {
        err = page->findItem(it->nsIndex, (ItemType)it->type, nullptr, it->entryIndex, item);
        it->entryIndex += item.span;
        ...
    } while(err != ESP_ERR_NVS_NOT_FOUND);
    it->entryIndex = 0;
}
```

With `key == nullptr` the in-RAM hash list is bypassed (`src/nvs_page.cpp:926` requires
`key != NULL`), so a full enumeration reads entry headers from flash. But it advances by
`item.span`, not one entry at a time — for 800-byte blobs the span is ~26, so it touches
**~1 header read per blob per page**, not per entry. Bounded above by `6 pages x 126 entries` for
the whole 24 KB partition.

Reading a blob is `Storage::readMultiPageBlob` (`src/nvs_storage.cpp:651-700`):

1. One `findItem` for `BLOB_IDX` — this one **does** use the hash list (key is non-null,
   `src/nvs_page.cpp:926-933`), a 24-bit-hash lookup in RAM giving the entry index directly.
2. Per chunk: one hash-accelerated `findItem` plus one `readVariableLengthItemData` bulk read of
   the chunk's data bytes.

The hash list is the reason lookups are cheap; it costs 128–640 bytes of RAM per page
(`nvs_flash.rst:368-370`), so **6 pages x up to 640 B = up to ~3.8 KB of RAM** held for the whole
24 KB partition (up to ~10 KB for a 64 KB partition — worth knowing before enlarging).

For 8–16 slots of ~800 bytes:

```
enumeration : one pass over <= 630 live entries, advancing by span
              -> ~16 header reads of 32 B  = ~512 B read + per-page scan overhead
reading all : 16 x (1 index lookup + 1 chunk lookup + ~800 B bulk read)
              = ~13 KB of flash reads, ~48 hash-accelerated lookups
```

Total flash traffic to list *and* fully read 16 animations is **~13 KB** — well under one page of
esp_partition read bandwidth concerns and dominated by memory-copy speed, not by NVS algorithmics.

**What I could not establish:** ESP-IDF publishes no benchmark numbers (µs or ms) for
`nvs_get_blob` or `nvs_entry_next`, and I found none in the v5.5.2 tree. The *shape* of the cost is
sourced above and is linear-with-small-constants; the absolute latency is not established here and
should be measured on the target with `esp_timer_get_time()` before promising a BLE connect budget.
The one clear design lever the source supports: **do not read all blob bodies at connect.** Enumerate
keys with the iterator, send the list (name + UUID, which requires reading the blobs — see below),
and read full keyframe payloads lazily on playback.

If connect-time latency turns out to matter, a sourced optimisation is to keep a small **index
blob** (one key holding all N `{uuid, name}` pairs, ~16 x 40 = 640 bytes) alongside the per-animation
blobs. One `nvs_get_blob` answers the whole "list animations" query; the 769-byte keyframe payloads
are only touched on playback. Cost: the index must be rewritten on every add/rename/delete, and
that rewrite is subject to the 2× rule of §3.3(a) — budget `2 + ceil(640/32)` = 22 entries twice.

---

## 6. The esp32c3-vs-esp32s3 discrepancy

**Evidence for esp32c3 (the current build):**

| Evidence | Source |
| --- | --- |
| `CONFIG_IDF_TARGET="esp32c3"`, `CONFIG_IDF_TARGET_ESP32C3=y`, `CONFIG_IDF_TARGET_ARCH="riscv"` | `sdkconfig` |
| `target: esp32c3` | `dependencies.lock:22` |
| `"target": "esp32c3"` | `build/project_description.json` |
| `build/project_elf_src_esp32c3.c` exists | build directory |
| `.vscode/c_cpp_properties.json` points at `riscv32-esp-elf-gcc` | `.vscode/c_cpp_properties.json` |
| `sdkconfig.defaults.esp32c3` sets `CONFIG_IDF_TARGET="esp32c3"` | tracked file |

**Evidence for esp32s3 (the developer's stated hardware):**

| Evidence | Source |
| --- | --- |
| `CONFIG_IDF_TARGET="esp32s3"` | `sdkconfig.old` (i.e. the target *before* the most recent `set-target`) |
| `sdkconfig.defaults.esp32s3` exists and additionally sets `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=80` — a deliberate, board-specific tuning the C3 defaults file does not have | tracked file |
| Commit `faa625b` "update for new esp32s3 board pinout, fix possibility of ble stack overflow" | `git log` |
| Commit `5617cb1` "reduce cpu frequency for esp32s3 for power savings" | `git log` |
| Source has live `#if CONFIG_IDF_TARGET_ESP32S3` branches for GPIO assignment | `main/led.h:16-18`, `main/servo.h:19-24` |

**Reading of the evidence.** The project is genuinely **multi-target by design** — four
`sdkconfig.defaults.<target>` files, `#if CONFIG_IDF_TARGET_*` pin maps in `led.h` and `servo.h`,
and a `CONFIG_IDF_TARGET_ESP32C3` branch in `main/boot.c:42`. The committed `sdkconfig` and the
`build/` tree are **build artifacts of whichever target was last selected on this machine**, not a
declaration of the product. `sdkconfig.old` still saying `esp32s3` is direct evidence that the
previous `idf.py set-target` state was S3 and someone then switched to C3 locally.

The *real hardware* signal is the commit history: two commits specifically adapt the firmware to an
S3 board (pinout, CPU frequency), and they are the most recent target-specific hardware work.
`sdkconfig` and `sdkconfig.old` are both **tracked in git** while `build/` is gitignored — tracking
a generated `sdkconfig` is what makes this ambiguous in the first place, and is worth fixing
independently of this research.

**Conclusion: the developer's "it's an S3" is the better-supported claim about the physical device;
the `esp32c3` in `sdkconfig` reflects the last local `set-target`, not the product.** I cannot
resolve it further from the repo alone — nothing here records a board part number, a module
variant, or a flash chip ID.

**Flash size on each — and why `2MB` is not evidence:**

`CONFIG_ESPTOOLPY_FLASHSIZE_2MB` is the *global ESP-IDF Kconfig default*
(`components/esptool_py/Kconfig.projbuild:112-121`: `choice ESPTOOLPY_FLASHSIZE ... default
ESPTOOLPY_FLASHSIZE_2MB`). **No** `sdkconfig.defaults*` file in this repo sets a flash size
(verified: `grep -rn FLASHSIZE sdkconfig.defaults*` → no matches). So `2MB` means "nobody ever
configured this", on either target.

- Setting a flash size *smaller* than the physical chip is harmless but caps the addressable
  region: the partition table cannot extend past `CONFIG_ESPTOOLPY_FLASHSIZE`. With `2MB`
  configured, the proposed `anim` partition at `0x110000..0x120000` is well inside the limit and
  works either way.
- Common devkit configurations (ESP32-C3-DevKitM-1 at 4 MB, ESP32-S3-DevKitC-1 at 8 MB) are
  **not** established by anything in this repo and are deliberately not asserted here.
- The actionable step is to read the physical size off the device
  (`esptool.py flash_id`) and set `CONFIG_ESPTOOLPY_FLASHSIZE` to match, in the per-target
  `sdkconfig.defaults.*` files. Until then, treat 2 MB as the *guaranteed floor* and design the
  partition table to fit inside it — which the §4.4 layout does.

---

## 7. Implications for the animation store

### 7.1 The entry budget

Everything in entries (32 bytes each), against a pool of **630** (§1.3).

Fixed, already-committed consumption:

| Item | Formula | Entries |
| --- | --- | --- |
| Namespace records (`lighting`, `servo_cal`, `anim_mode`, new animations ns, Bluedroid `bt_config.conf`) | 1 each (`src/nvs_storage.cpp:626`) | 5 |
| `lighting/config` blob — `lighting_data_t` = enum(4) + speed(1) + 32x`rgb_color_t`(96) + count(1), padded ≈ 104 B (`main/types/lighting_types.h:23,48-53`) | `2 + ceil(104/32)` | 6 |
| `servo_cal/calibration` blob — 8 B (`main/servo_calibration.c:144`) | `2 + 1` | 3 |
| `anim_mode/mode` blob — 4 B (`main/animation_mode.c:168`) | `2 + 1` | 3 |
| **Subtotal (application)** | | **17** |
| Bluedroid bond database (`bt_cfg_key0…`) — size unknown, grows per bonded peer | reserve 2 KB | **64** |
| **Fixed subtotal** | | **81** |

Required free headroom (§3.3): **166** entries.

```
630 (pool)  -  81 (fixed)  -  166 (headroom)  =  383 entries for animations
383 / 28 entries-per-slot                     =  13.7 slots
```

### 7.2 Recommended slot count

**12 slots**, on the stock 24 KB partition.

```
12 slots x 28 entries = 336 entries = 10 752 bytes
336 + 81 (fixed)      = 417 entries used at full capacity
630 - 417             = 213 entries free (6 816 B, 1.7 pages)
                        vs. 166 required headroom -> 47 entries (1.5 KB) of margin
```

12 is chosen rather than 16 because 16 slots consume 448 entries; `448 + 81 = 529`, leaving only
101 free entries — **below the 166-entry headroom floor**, which puts blob rewrites into exactly
the "fits on paper, fails in practice" regime of §3.3. And the Bluedroid bond blob is a genuine
unknown: if it exceeds the 2 KB reserved above, 16 slots fail first.

If a smaller, unconditionally safe number is preferred: **8 slots** = 224 entries;
`224 + 81 = 305`, leaving 325 free entries (10.4 KB, ~2.6 pages) — roughly 2× the required
headroom, comfortable even if the bond database doubles.

Design rules that follow from the source:

- **One blob per animation** (name + UUID + keyframes in one serialized record), not three keys.
  Saves 4 entries per slot and makes each save atomic under the `VER` scheme (§2.3, §2.5).
- **Fixed slot keys** (`a0`…`a11`, well inside the 15-char limit) so slot reuse is an overwrite,
  which the byte-identical short-circuit at `src/nvs_storage.cpp:476-481` makes free when nothing
  changed.
- **Check `nvs_get_stats().available_entries` before accepting a new animation over BLE** and
  refuse with a clean BLE error rather than letting `nvs_set_blob` return
  `ESP_ERR_NVS_NOT_ENOUGH_SPACE` mid-transfer. Require `available_entries >= 28 + 166`.
- **Don't read every blob body on connect.** Enumerate with `nvs_entry_find(..., NVS_TYPE_BLOB, ...)`,
  or keep a separate small index blob (§5.2).
- **Handle `ESP_ERR_NVS_NOT_ENOUGH_SPACE` as a first-class outcome**, not an assert.

### 7.3 Does the stock 24 KB partition suffice?

**Yes for 12 slots — but a custom partition table is nevertheless warranted, and it is cheap.**

Arguments that 24 KB suffices: the arithmetic above closes with margin at 12 slots; the partition
is 6 pages, comfortably above the 3-page minimum (`nvs_flash.rst:377`); and no partition-table
change means no risk to the existing settings and BLE bonds.

Arguments for the custom table, which I find stronger:

1. **The shared-partition risk is real and unquantified.** Every animation the user saves competes
   with the Bluedroid bonding database in the same `nvs`. A bonding write failing because the user
   filled the animation store is a bad failure mode, and NVS gives no per-namespace quota.
2. **Espressif's own guidance points that way:** "If using NVS API to store a lot of data, increase
   the NVS partition size from the default 0x6000 bytes" (`partition-tables.rst:178`). ~10 KB of
   user blobs in a 24 KB partition shared with the BLE stack qualifies.
3. **The upgrade is non-destructive and nearly free.** Adding `anim, data, nvs, 0x110000, 0x10000`
   in the unallocated tail leaves `nvs`, `phy_init` and `factory` at identical offsets, so existing
   device data survives (`partition-tables.rst:319`, §4.4). One CSV file, one Kconfig switch, and
   `nvs_flash_init_partition("anim")` + `nvs_open_from_partition(...)` in the firmware.
4. **It removes the slot-count constraint entirely.** 64 KB = 1890 usable entries ≈ 67 slots, so
   16 (or 32) slots stop being a capacity conversation.

**Recommendation:** ship 12 slots either way; put them in a dedicated `anim` NVS partition carved
from the unallocated tail rather than in the shared 24 KB `nvs`. Before committing the `0x110000`
offset, confirm the physical flash size on the actual board (§6) — the whole layout assumes at
least 2 MB, which is currently only a Kconfig default, not a verified hardware fact.

### 7.4 Open items I could not resolve from primary sources

- **Absolute timings** for `nvs_get_blob` / `nvs_entry_next` on either target. ESP-IDF publishes
  none; measure on hardware.
- **Actual size of the Bluedroid `bt_config.conf` blob chain** on this firmware with N bonded
  peers. It is generated at runtime from a text serialization (`components/bt/common/osi/config.c:403-500`)
  and cannot be bounded statically. Measure with `nvs_get_stats` on a device that has bonded a few
  times.
- **Whether shipped devices really have ≥2 MB flash, and what (if anything) currently occupies
  `0x110000..0x200000`** on them.
- **Which target the shipped hardware is.** §6 gives the evidence; it favours ESP32-S3 but is not
  conclusive from the repo alone.
