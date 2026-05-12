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

dl1_cache = new Cache(32768, 64, 8, LRU);
dl2_cache = new Cache(262144, 64, 16, LRU); 

## Pin Instrumentation

The tool uses two Pin callbacks. The **IMG callback** identifies the address range of the main executable so only its instructions are instrumented, excluding library code. The **INS callback** inserts an instruction counter increment and a `RecordMemAccess` call for every memory read and write operand on each instruction in range.

Read-write operands (e.g., `add [rax], 1`) are counted as two separate accesses, consistent with real hardware behavior.

## How to Build

```bash
# Navigate to the tool directory
cd $PIN_ROOT/source/tools/MyPinTool

# Clean and compile (64-bit)
make clean
make obj-intel64/MyPinTool.so

# Run Pin with your cache simulator on the target program
$PIN_ROOT/pin -t obj-intel64/MyPinTool.so -- ./program

# View simulation results
cat cache_stats.out

valgrind --tool=cachegrind --D1=32768,8,64 --LL=262144,16,64 ./program

```

## Cache Specifications

- **L1 Data Cache:** 32 KB, 8-way associative, 64B block size  
- **L2 Data Cache:** 256 KB, 16-way associative, 64B block size  
- **Latencies:**  
  - L1: 1 cycle  
  - L2: 10 cycles  
  - RAM: 100 cycles  


# Cache Simulator comparison with Cachegrind (Industrial Standard)

## L1 Data Cache Misses Comparison

| Implementation   | Simulator L1 Misses | Valgrind L1 Misses | Error (%)    |
|------------------|---------------------|--------------------|--------------|
| Naive MM         | 16,881,963         | 16,882,150        | **0.0011%** |
| Tiled MM         | 163,589            | 163,998           | **0.249%**  |

## L2 Data Cache Misses Comparison

| Implementation   | Simulator L2 Misses | Valgrind L2 Misses | Error (%)   |
|------------------|---------------------|--------------------|-------------|
| Naive MM         | 1,221,364           | 1,209,336          | **0.993%** |
| Tiled MM         | 96,976              | 97,013             | **0.038%** |

### Error Calculation
**Error (%) =** `|Simulator - Valgrind| / Valgrind × 100`


# Cache Simulator results

|           Benchmark          |     Metric    |  LRU   |  FIFO  |   LFU   | Random |
|------------------------------|---------------|--------|--------|---------|--------|
|   Matrix multiplication      | L1 hit rate   | 93.35% | 93.12% |  90.99% | 93.11% |
|                              | L2 hit rate   | 92.77% | 92.87% |  32.36% | 97.68% |
|                              | AMAT (cycles) |  2.15  |  2.18  |   8.00  |  1.85  |
|------------------------------|---------------|--------|--------|---------|--------|
| Tiled matrix multiplication  | L1 hit rate   | 99.94% | 99.95% |  91.76% | 99.34% |
|                              | L2 hit rate   | 59.09% | 48.64% |  90.74% | 96.80% |
|                              | AMAT (cycles) |  1.03  |  1.033 |   2.59  |  1.09  |


## Performance Summary

- **Naive Matrix Multiplication:**  
  Random replacement policy achieved the best performance with an AMAT of **1.85 cycles/access**, while LFU performed significantly worse with an AMAT of **8.00 cycles/access**.

- **Tiled Matrix Multiplication:**  
  LRU and FIFO achieved near-ideal performance with AMAT values close to **1.03 cycles/access**, reducing L1 miss rates to below **0.1%**.

---

## Key Findings

### LRU Thrashing
In the naive implementation, power-of-two matrix strides (`256 × 256`) caused severe conflict misses. LRU systematically evicted useful cache lines, allowing the Random policy to outperform it through probabilistic retention of required data.

### Tiling Impact
Tiling constrained the active working set to fit within L1 cache capacity, significantly improving temporal and spatial locality. This demonstrates how software-level locality optimizations can compensate for hardware replacement policy limitations.

### LFU Pollution
LFU performed poorly across all experiments because frequently accessed data from earlier execution phases remained cached for too long. This cache pollution prevented newly required tiles from entering the cache efficiently.

## Completed Features

### Phase 0 — Core Cache Data Structures
Implemented the foundational cache simulator architecture:

- `CacheBlock`
  - Valid bit
  - Dirty bit
  - Tag storage
  - Frequency counter (for LFU)

- `CacheSet`
  - Configurable associativity
  - Block lookup and insertion
  - Replacement handling

- `Cache`
  - Address decomposition (tag/index/block offset)
  - Access tracking
  - Hit/miss statistics
  - AMAT calculation support

---

### Phase 1 — LRU Replacement Policy
Implemented **Least Recently Used (LRU)** replacement using linked-list ordering.

Features:
- Recency tracking on every hit
- Most recently used blocks moved to the front
- Least recently used block evicted from the back

---

### Phase 2 — FIFO Replacement Policy
Implemented **First-In First-Out (FIFO)** replacement.

Features:
- Preserves insertion order
- Oldest inserted block selected for eviction
- Lower bookkeeping overhead than LRU

---

### Phase 3 — Pin Instrumentation
Integrated the simulator with Intel Pin for dynamic binary instrumentation.

Features:
- Instruction counting
- Memory read tracing
- Memory write tracing
- Main executable filtering
- Runtime statistics generation

---

### Phase 4 — Additional Replacement Policies
Added support for more advanced and comparative replacement strategies.

#### LFU (Least Frequently Used)
- Tracks access frequency per cache block
- Evicts the least frequently accessed block

#### Random Replacement
- Random victim selection using `std::mt19937`
- Useful as a baseline policy for comparison studies

---

### Phase 5 — Multi-Level Cache Hierarchy
Implemented a two-level cache hierarchy:

- L1 Data Cache
- L2 Data Cache

Features:
- L1 miss triggers L2 lookup
- Independent statistics for each cache level
- Configurable size, associativity, and block size
- Inclusive hierarchy behavior

---

### Phase 6 — Write-Back Cache Support
Implemented write-aware cache behavior with dirty block handling.

Features:
- Dirty bit tracking
- Write-back propagation from L1 → L2
- Dirty eviction handling
- Write misses correctly initialize dirty blocks
- Support for realistic memory hierarchy simulation

---

### Phase 7 — Performance Analysis Metrics
Added system-level performance evaluation metrics.

Features:
- Hit rate and miss rate reporting
- AMAT (Average Memory Access Time) calculation
- Configurable latency parameters:
  - L1 latency
  - L2 latency
  - Main memory latency
