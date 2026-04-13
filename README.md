# Cache Simulator — Intel Pin Tool

A dynamic binary instrumentation tool built on **Intel Pin** that simulates an L1 data cache, collecting memory access statistics from real program execution traces.

## Overview

This tool instruments a target binary at runtime, intercepts all memory read and write operations within the main executable, and feeds them through a configurable cache simulator. At termination, it outputs a summary of cache performance including hit rate, miss count, and total accesses.

## Architecture

The simulator is organized into three layered classes that mirror real hardware.

**CacheBlock** is the smallest unit, representing a single cache line. It holds a `valid` bit (whether the block contains meaningful data) and a `tag` used for address lookup.

**CacheSet** represents one set in a set-associative cache. It manages a list of `CacheBlock` entries and implements the replacement policy. With **LRU**, a hit moves the accessed block to the front of the list, and a miss evicts the block at the back. With **FIFO**, insertion order is preserved and the oldest block is always evicted on a miss, regardless of access recency.

**Cache** is the top-level object. It handles address decomposition and dispatches accesses to the correct set, then tracks global statistics.

```
Address breakdown:
  offset  = addr % blockSize
  index   = (addr / blockSize) % numSets
  tag     = (addr / blockSize) / numSets
```

## Current Configuration

The cache is instantiated in `main()` with the following parameters:

| Parameter      | Value    |
|----------------|----------|
| Cache Size     | 32 KB    |
| Block Size     | 64 bytes |
| Associativity  | 8-way    |
| Replacement    | FIFO     |
| Sets (derived) | 64       |

To switch to LRU, change the last argument in `main()`:

```cpp
dl1_cache = new Cache(32768, 64, 8, LRU);
```

## Pin Instrumentation

The tool uses two Pin callbacks. The **IMG callback** identifies the address range of the main executable so only its instructions are instrumented, excluding library code. The **INS callback** inserts an instruction counter increment and a `RecordMemAccess` call for every memory read and write operand on each instruction in range.

Read-write operands (e.g., `add [rax], 1`) are counted as two separate accesses, consistent with real hardware behavior.

## Output

Results are written to `cache_stats.out`, configurable via the `-i` flag:

```
L1 DATA CACHE SIMULATION RESULTS
Total Instructions  : <count>
Total Mem Accesses  : <count>
Cache Hits          : <count>
Cache Misses        : <count>
Hit Rate            : <percent> %
```

## Building and Running

```bash
# Build (from your Pin tool directory)
make

# Run against a target binary
pin -t obj-intel64/cache_sim.so -- ./your_program

# Custom output file
pin -t obj-intel64/cache_sim.so -i my_output.out -- ./your_program
```

## Completed Phases

**Phase 0** — Core data structures: `CacheBlock`, `CacheSet`, `Cache`

**Phase 1** — LRU replacement policy

**Phase 2** — FIFO replacement policy

**Phase 3** — Pin instrumentation (instruction counting + memory access tracing)

## Planned Work

**Multi-Level Cache Hierarchy** — Extend the simulator to model L1 → L2 interactions, where an L1 miss triggers an L2 lookup. This includes separate hit/miss statistics per level, configurable size and associativity for each level, and options for inclusive vs. exclusive cache policies.

**Write Policies** — Add support for write-through (cache and next memory level updated simultaneously on a write hit) and write-back (only the cache block is updated on a write hit, marked dirty, and written to the next level on eviction). Write-allocate and no-write-allocate behavior will also be configurable.

