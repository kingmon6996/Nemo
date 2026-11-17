# Write-Ahead Log (WAL)

The WAL manager stores checksummed records in append-only segments, batches
flush requests, and tracks the highest LSN confirmed durable. Page writes use
physical before/after images. The disk manager durably appends each page image
before writing that page and stamps non-header pages with the assigned LSN.
SQL autocommit statements and explicit transactions persist their pages before
their commit markers are acknowledged. Checkpoint-aware recovery, safe segment
truncation, and process-crash validation are implemented.

## Storage layout

For a database opened as `.nemo/<name>/data.nm`, WAL segments are stored in
`.nemo/<name>/wal/`, with its checkpoint master record at
`.nemo/<name>/master.nm`. A standalone database file uses a sibling directory
named `<filename>.wal` and a uniquely named `<filename>.master.nm` master record.

Segments have a 24-byte header and a maximum size of 256 MB. A new numbered
segment is opened before the next batch would exceed that limit. Existing
segments are scanned and validated on open; a malformed or torn record fails
open rather than being silently overwritten. Segments that are
no longer required by the latest durable checkpoint are truncated after the
checkpoint master record is atomically published.

Checkpoints flush data pages and the WAL before publishing the master record.
They are triggered after 64 MiB of WAL growth or five minutes (checked by a
30-second background worker), and on clean database close.

## Record format

Each log record has a 41-byte header followed by its payload:

| Bytes | Field | Description |
|:---|:---|:---|
| 0–7 | `lsn` | Monotonically increasing log sequence number |
| 8–15 | `txn_id` | Transaction ID |
| 16–23 | `prev_lsn` | Previous record in that transaction |
| 24–31 | `undo_next_lsn` | Next LSN to undo for a CLR |
| 32 | `type` | Record type |
| 33–36 | `payload_len` | Payload size |
| 37–40 | `checksum` | CRC32 over the record fields and payload, excluding this field |

The API currently emits begin, commit, and abort markers for explicit
transactions. SQL autocommit DML and supported DDL also run inside implicit
transactions. A `PageWrite` payload is encoded as:

| Bytes | Field |
|:---|:---|
| 0–7 | Page ID |
| 8–11 | Page size |
| 12 onward | Full before image followed by full after image |

Page-image records are redoable and undoable. Recovery stamps the replayed
LSN into non-header pages and recalculates their checksums.

## Flush and group commit

In `Normal` mode, `flush_up_to(lsn)` waits for up to 5 ms for more records,
unless the 4 MB buffer threshold is reached first. Appending remains available
during that wait. `Full` and `Off` modes do not wait for the batching window;
explicit flush requests still perform a durable flush.

The buffer is double-buffered: a flush swaps the active buffer under the append
lock, writes the detached batch, then performs an OS-level durable flush
(`FlushFileBuffers` on Windows or `fsync` on POSIX). The durable LSN advances
only to the last LSN in that batch. Oversized records and unappended flush
targets are rejected. Write/flush errors are surfaced and put the manager into
a failed state so later operations cannot report success.

Explicit transaction commits persist their changed pages under the transaction
WAL context before appending and flushing the commit marker. WAL failures are
returned as API errors. The in-memory row-visibility protocol is still part of
the unfinished Phase 5 concurrency work and should not be treated as a
lock-free visibility guarantee.

## WAL-before-data

The buffer pool asks the WAL manager to flush a page's existing LSN before
writing that page. The disk manager then appends and flushes a new page-image
record, assigns its LSN to the page, and only then writes the data file.
Database bootstrap writes that happen before WAL initialization are the
exception; they contain no user data.

The group-commit tests cover the batching window, the exact 4 MB threshold, and
durable-LSN tracking. Segment rollover is exercised at the 256 MB limit.
Injected write and durable-flush failures verify errors are surfaced, the
durable LSN does not advance, and the manager remains failed.

## Segment header

Each segment begins with:

| Bytes | Field |
|:---|:---|
| 0–3 | Magic `0x4B4C5741` |
| 4–7 | Format version |
| 8–15 | Segment ID |
| 16–23 | Starting LSN |

## Related

- [HOW_IT_WORKS.md](../HOW_IT_WORKS.md) — Architecture overview.
- [buffer_pool.md](buffer_pool.md) — Buffer-pool flush behavior.
- [crash_recovery.md](crash_recovery.md) — Recovery design and remaining gaps.
