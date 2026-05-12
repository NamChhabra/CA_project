#include <algorithm> 
#include <vector> 
#include <list>
#include <iostream>
#include <fstream>
#include <random>
#include <iomanip>
#include "pin.H"

using namespace std; 

std::ofstream OutFile;

// PHASE 0, 1, 2, 3: CACHE SIMULATOR CLASSES

enum ReplacementPolicy {
    LRU,
    FIFO,
    LFU,
    RAN
};

// 1. Cache Block (Holds valid bit, dirty bit, freq, and tag)
struct CacheBlock {
    bool valid;
    bool dirty; 
    UINT64 tag;
    UINT32 freq;
    CacheBlock() : valid(false), dirty(false), tag(0), freq(0) {}
};

// 2. Cache Set (Handles Associativity and Replacement Policy)
class CacheSet {
private:
    UINT32 associativity;
    ReplacementPolicy policy;
    list<CacheBlock> blocks; 

    std::mt19937 gen;
    std::uniform_int_distribution<int> dist;

public:
    CacheSet(UINT32 assoc, ReplacementPolicy pol) : associativity(assoc), policy(pol), gen(random_device{}()),
          dist(0, assoc - 1) {}

    // Accepts isWrite, returns evicted state by reference
    bool access(UINT64 tag, bool isWrite, bool& evictedDirty, UINT64& evictedTag) {
        evictedDirty = false; // Default to false

        // Search the set for a matching tag
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->valid && it->tag == tag) {
                // HIT!
                if (isWrite) it->dirty = true; // Mark as dirty on write

                if (policy == LFU || policy == FIFO || policy == RAN){
                    it->freq++;
                }
                else if (policy == LRU) {
                    CacheBlock hitBlock = *it;
                    hitBlock.freq++;
                    blocks.erase(it);
                    blocks.push_front(hitBlock);
                }
                return true; 
            }
        }

        // MISS!
        CacheBlock newBlock;
        newBlock.valid = true;
        newBlock.dirty = isWrite; // starts dirty on write miss
        newBlock.tag = tag;
        newBlock.freq = 1;

        if (blocks.size() < associativity) {
            // Still have room in this set
            blocks.push_front(newBlock);
        } else {
            // Set is full. Find the victim to evict.
            auto victim = blocks.end();

            if (policy == LRU || policy == FIFO) {
                victim = std::prev(blocks.end()); // The block at the back
            }
            else if (policy == LFU) {
                victim = blocks.begin();
                for (auto it = blocks.begin(); it != blocks.end(); ++it) {
                    if (it->freq < victim->freq) {
                        victim = it;
                    }
                }
            }
            else if (policy == RAN) {
                int idx = dist(gen);
                victim = blocks.begin();
                std::advance(victim, idx); 
            }

            // if the chosen victim is dirty before erasing it
            if (victim->dirty) {
                evictedDirty = true;
                evictedTag = victim->tag;
            }

            blocks.erase(victim);
            blocks.push_front(newBlock);
        }
        return false;
    }
};

// 3. The Cache Class
class Cache {
private:
    UINT32 numSets;
    UINT32 blockSize;
    UINT32 associativity;
    ReplacementPolicy policy;
    
    vector<CacheSet> sets;

    // Statistics
    UINT64 hits;
    UINT64 misses;
    UINT64 accesses;

public:
    Cache(UINT32 sizeBytes, UINT32 bSize, UINT32 assoc, ReplacementPolicy pol) {
        blockSize = bSize;
        associativity = assoc;
        policy = pol;
        numSets = sizeBytes / (blockSize * associativity);

        for (UINT32 i = 0; i < numSets; ++i) {
            sets.push_back(CacheSet(associativity, policy));
        }

        hits = misses = accesses = 0;
    }

    // Accepts isWrite, processes Write-Backs
    bool access(UINT64 addr, bool isWrite, bool& evictWB, UINT64& wbAddr) {
        accesses++;
        
        UINT64 blockAddress = addr / blockSize; 
        UINT64 index = blockAddress % numSets;  
        UINT64 tag = blockAddress / numSets;    

        bool evictedDirty = false;
        UINT64 evictedTag = 0;

        // Pass write state down to the set
        bool hit = sets[index].access(tag, isWrite, evictedDirty, evictedTag);
        
        // If a dirty block was evicted, rebuild its absolute address
        if (evictedDirty) {
            evictWB = true;
            wbAddr = ((evictedTag * numSets) + index) * blockSize;
        } else {
            evictWB = false;
        }

        if (hit) {
            hits++;
        } else {
            misses++;
        }
        return hit;
    }

    UINT64 getHits() const { return hits; }
    UINT64 getMisses() const { return misses; }
    UINT64 getAccesses() const { return accesses; }
    double getHitRate() const { 
        return accesses == 0 ? 0.0 : (double)hits / accesses * 100.0; 
    }
    double getMissRate() const { 
        return accesses == 0 ? 0.0 : (double)misses / accesses; 
    }
    string getPolicyName() const {
        if (policy == LRU) return "LRU";
        if (policy == FIFO) return "FIFO";
        if (policy == LFU) return "LFU";
        return "RANDOM";
    }
};

// -------------------------------------------------------------------------
// PINTOOL INSTRUMENTATION
// -------------------------------------------------------------------------

static UINT64 icount = 0;
ADDRINT mainLow = 0, mainHigh = 0;

// Instantiate our caches
Cache* dl1_cache; 
Cache* dl2_cache;

VOID docount() { icount++; }

// Unified memory access hook, now aware of Read/Write and Write-Backs
VOID RecordMemAccess(VOID* addr, bool isWrite) {
    bool evictWB = false;
    UINT64 wbAddr = 0;

    // 1. Try L1 Cache
    if (!dl1_cache->access((UINT64)addr, isWrite, evictWB, wbAddr)) {
        // L1 MISS: Fetch the missing block from L2 (This is a Read from L2's perspective)
        bool dummyWB; UINT64 dummyAddr;
        dl2_cache->access((UINT64)addr, false, dummyWB, dummyAddr);
    }

    // 2. Did L1 kick out a dirty block to make room?
    if (evictWB) {
        // L1 WRITE-BACK: Write the modified block down to L2
        bool dummyWB; UINT64 dummyAddr;
        dl2_cache->access(wbAddr, true, dummyWB, dummyAddr);
    }
}

VOID Instruction(INS ins, VOID* v) {
    // if (INS_Address(ins) <= mainHigh && INS_Address(ins) >= mainLow) {
        
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);   

        UINT32 memOperands = INS_MemoryOperandCount(ins);
        for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
            
            // Route Reads
            if (INS_MemoryOperandIsRead(ins, memOp)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess, 
                               IARG_MEMORYOP_EA, memOp, 
                               IARG_BOOL, false, // isWrite = false
                               IARG_END);
            }
            
            // Route Writes
            if (INS_MemoryOperandIsWritten(ins, memOp)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess, 
                               IARG_MEMORYOP_EA, memOp, 
                               IARG_BOOL, true,  // isWrite = true
                               IARG_END);
            }
        //}
    }
}

KNOB< string > KnobInsCountFile(KNOB_MODE_WRITEONCE, "pintool", "i", "cache_stats.out", "specify output file name");

VOID Fini(INT32 code, VOID* v) {
    OutFile.setf(std::ios::showbase);
    OutFile << fixed << setprecision(4); // Clean up decimal outputs

    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile << "L1 DATA CACHE SIMULATION RESULTS (" << dl1_cache->getPolicyName() << ")\n";
    OutFile << "-------------------------------------------------------------------------------\n";  
    
    OutFile << "Total Instructions  : " << icount << "\n";
    OutFile << "Total Mem Accesses  : " << dl1_cache->getAccesses() << "\n";
    OutFile << "Cache Hits          : " << dl1_cache->getHits() << "\n";
    OutFile << "Cache Misses        : " << dl1_cache->getMisses() << "\n";
    OutFile << "Hit Rate            : " << dl1_cache->getHitRate() << " %\n";
    
    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile << "L2 DATA CACHE SIMULATION RESULTS (" << dl2_cache->getPolicyName() << ")\n";
    OutFile << "-------------------------------------------------------------------------------\n";  
    
    OutFile << "Total Mem Accesses  : " << dl2_cache->getAccesses() << "\n";
    OutFile << "Cache Hits          : " << dl2_cache->getHits() << "\n";
    OutFile << "Cache Misses        : " << dl2_cache->getMisses() << "\n";
    OutFile << "Hit Rate            : " << dl2_cache->getHitRate() << " %\n";

    // AMAT CALCULATION
    int L1_LATENCY = 1;     // dummy cycles
    int L2_LATENCY = 10;    // dummy cycles
    int MEM_LATENCY = 100;  // dummy cycles
    
    double amat = L1_LATENCY + (dl1_cache->getMissRate() * (L2_LATENCY + (dl2_cache->getMissRate() * MEM_LATENCY)));

    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile << "SYSTEM PERFORMANCE\n";
    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile << "L1 Latency          : " << L1_LATENCY << " cycles\n";
    OutFile << "L2 Latency          : " << L2_LATENCY << " cycles\n";
    OutFile << "Memory Latency      : " << MEM_LATENCY << " cycles\n";
    OutFile << "AMAT                : " << amat << " cycles/access\n";
    OutFile << "-------------------------------------------------------------------------------\n";  
    
    OutFile.close();

    delete dl1_cache;
    delete dl2_cache;
}

INT32 Usage() {
    cerr << "This tool simulates a Data Cache." << std::endl;
    cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
    return -1;
}

VOID Image(IMG img, VOID *v) {
    if (IMG_IsMainExecutable(img)) {
        mainLow = IMG_LowAddress(img);
        mainHigh = IMG_HighAddress(img);
    }
}

int main(int argc, char* argv[]) {
    if (PIN_Init(argc, argv)) return Usage();

    // Initialize L1 Cache 
    dl1_cache = new Cache(32768, 64, 8, LRU);
    
    // Initialize L2 Cache
    dl2_cache = new Cache(262144, 64, 16, LRU); 

    OutFile.open(KnobInsCountFile.Value().c_str());

    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}
