# Buffer Pool

The Nemo buffer pool is an in-process page cache that sits between the query engine and the disk. It keeps hot 8 KB pages in RAM and evicts cold ones using a **CLOCK (second-chance)** algorithm. Every dirty page is flushed to `data.nm` before the buffer pool frame is reused or the database is closed.

---

## Configuration

```python
import nemo

# Auto-detect: 1/4 of available system RAM, clamped to [32 MB, 8 GB]
db = nemo.connect("mydb")

# Pin the buffer pool at exactly 512 MB
db = nemo.connect("mydb", buffer_pool_mb=512)
```

| Parameter | Type | Behaviour |
|:---|:---|:---|
| *(omitted)* | auto | 1/4 of available RAM, clamped 32 MB – 8 GB |
| `buffer_pool_mb=N` | `int` | Exactly N MB, clamped to [32 MB, 8 GB] |

> [!NOTE]
> "Available RAM" means free physical memory at open time, **not** total installed RAM.
> On a 16 GB machine with 8 GB free, the auto-detected pool is 2 GB.

---

## Architecture

```
nemo.connect("mydb", buffer_pool_mb=256)
         │
         ▼  Python layer
  _validate_db_name()
  _resolve_db_path()   → .nemo/mydb/data.nm
         │
         ▼  C++ layer  (database.cpp :: Database::open)
  compute_buffer_pool_bytes(256 MB)       ← clamp [32 MB, 8 GB]
  bytes_to_frame_count(pool_bytes, 8192)  ← pool_bytes / page_size
         │
         ▼
  DiskManager::open(".nemo/mydb/data.nm", Config)
  WALManager(".nemo/mydb/wal/")
  BufferPool(frame_count, disk_manager, wal_manager)
```

### CLOCK Replacer

The `ClockReplacer` maintains a circular array of `FrameState` slots. Each slot tracks:
- `is_pinned` — frame is in active use; never evict
- `reference_bit` — set on access; cleared on CLOCK sweep
- `is_empty` — frame has never held a page

On each eviction request:
1. Walk the clock hand forward until a frame with `reference_bit == false` and `is_pinned == false` is found.
2. If `reference_bit == true`, clear it (second-chance) and advance.
3. The found frame is the victim; if dirty → flush to disk first.

### Pin / Unpin

Every `fetch_page()` call **pins** the frame atomically:
- `pin_count` increments → frame is safe from eviction.
- The caller gets a `PageHandle` RAII wrapper.
- When `PageHandle` goes out of scope → `unpin_page()` is called → `pin_count` decrements.
- When `pin_count` reaches 0 → frame is re-eligible for eviction.

### WAL-before-data Rule

When a WAL manager is available, before any dirty page is written to `data.nm`,
the buffer pool calls:
```cpp
wal_manager->flush_up_to(page_lsn);
```
This ensures the WAL record for every change is durable before the page itself hits disk.
The database constructs the WAL manager before the buffer pool so pages loaded
on open and later SQL work use the same pool. If WAL initialization fails, the
pool can still run without the WAL-before-data check; end-to-end WAL failure
handling remains a Phase 3 gap.

---

## Flush & Checkpoint

```python
db.flush()       # flush all dirty buffer-pool pages to data.nm
db.checkpoint()  # alias for flush() until Phase 3 wires WAL checkpointing
```

Both are also called automatically in `db.close()` and `__exit__`.

---

## Statistics

```python
stats = db.statistics()
print(stats.buffer_hits)    # pages served from RAM
print(stats.buffer_misses)  # pages read from disk
print(stats.buffer_pool_frames)  # allocated page frames
```

Hit ratio = `hits / (hits + misses)`. A well-warmed pool should exceed 95% on OLTP workloads.
`buffer_pool_frames` reports the actual configured frame count.

---

## Memory Layout

| Component | RAM cost |
|:---|:---|
| Each buffer frame | `page_size` bytes = 8 KB |
| Frame metadata | ~64 bytes/frame (`BufferFrame` struct) |
| Page table | ~40 bytes/entry (hash map, one per cached page) |
| CLOCK array | ~3 bytes/frame |

For a 512 MB pool at 8 KB pages: **65,536 frames**, ~512 MB data + ~4 MB metadata.

---

## Related

- [HOW_IT_WORKS.md](../HOW_IT_WORKS.md) — Full architecture overview.
- [docs/connect.md](connect.md) — `nemo.connect()` reference.
- Phase 3 (WAL) will complete durable commit ordering and WAL failure handling.

