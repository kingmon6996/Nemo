# Crash Recovery

Nemo recovers page-image WAL records with a checkpoint-aware startup redo/undo
pass. This is a working subset of ARIES.

## Page-write record

`DiskManager::write_page()` captures the page's on-disk before image and the
requested after image in a `PageWrite` record. It flushes that record before
writing the data page. Non-header pages are stamped with the record LSN and a
new checksum. Page zero is the database header and is replayed in WAL order;
it has no standard page header or page LSN.

## Startup order

For an existing file-backed database, startup opens the disk and WAL managers,
constructs the buffer pool, runs recovery, refreshes the database header and
free-page bitmap from the recovered file, and only then loads SQL metadata.
Invalid WAL, malformed page images, failed recovery writes, or failed header/
bitmap reloads cause `Database::open()` to fail.

Standalone database files use a sibling `<filename>.wal` directory. A database
at `.nemo/<name>/data.nm` uses `.nemo/<name>/wal/`. Its durable checkpoint
master record is `.nemo/<name>/master.nm`; standalone databases use a sibling
`<filename>.master.nm` file so databases sharing a parent directory cannot
overwrite one another's checkpoint metadata.

## Current redo/undo behavior

Analysis scans from the selected checkpoint through the WAL and records each
transaction's last LSN and whether it committed or ended. Redo scans page-image
and compensation records in LSN order. It skips a non-header page when its page
LSN is already at least the record LSN; otherwise it applies the after image,
stamps the LSN, and recalculates the checksum.

Transactions without a durable commit/end marker are losers. Undo follows their
previous-LSN chain from newest to oldest. A page-write undo appends and flushes
a CLR before applying the before image. Recovery then appends abort/end markers
and flushes the repaired data file.

Analysis resumes from the latest valid checkpoint master record, restores the
checkpoint transaction table and dirty-page table, and tracks subsequent WAL
records. Redo starts at the earliest dirty-page recLSN, or at the checkpoint
when the dirty-page table is empty. Checkpoint publication follows the data
flush and checkpoint-end WAL flush; only then may WAL segments older than the
checkpoint and all active transaction/page recovery requirements be removed.
Automatic checkpoints are triggered after 64 MiB of WAL growth or five
minutes, checked by a 30-second worker, and are also performed on clean close.
Recovery tests cover restart after process termination, committed data
preservation, loser-transaction undo, checkpoint-state restoration, and safe
segment truncation.

## Remaining work

- Complete persisted row visibility and rollback semantics in the later
  concurrency phase.

See [wal.md](wal.md) for record layout and durability ordering.
