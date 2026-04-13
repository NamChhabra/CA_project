#include <algorithm> 
#include <vector> 
#include <list>
#include <iostream>
#include <fstream>
#include "pin.H"

using namespace std; 

std::ofstream OutFile;

// -------------------------------------------------------------------------
// PHASE 0, 1, 2, 3: CACHE SIMULATOR CLASSES
// -------------------------------------------------------------------------

// Helper function to calculate log base 2 for integer math
UINT32 calcLog2(UINT32 val) { // Block size must be in power of 2 for correct working. Madhav pls make it more robust.
    UINT32 res = 0;
    while (val >>= 1) res++;
    return res;
}

enum ReplacementPolicy {
    LRU,
    FIFO
};

// 1. Cache Block (Holds the valid bit and tag)
struct CacheBlock {
    bool valid;
    UINT64 tag;
    CacheBlock() : valid(false), tag(0) {}
};

// 2. Cache Set (Handles Associativity and Replacement Policy)
class CacheSet {
private:
    UINT32 associativity;
    ReplacementPolicy policy;
    list<CacheBlock> blocks; // actually a linked list

public:
    CacheSet(UINT32 assoc, ReplacementPolicy pol) : associativity(assoc), policy(pol) {}

    // Returns true on Hit, false on Miss
    bool access(UINT64 tag) {
        // Search the set for a matching tag
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->valid && it->tag == tag) {
                // HIT! 
                
                if (policy == LRU) {
                    // LRU Policy: Move this accessed block to the front
                    CacheBlock hitBlock = *it;
                    blocks.erase(it);
                    blocks.push_front(hitBlock);
                }
                // If FIFO: Do absolutely nothing. The order it entered doesn't change.
                
                return true; 
            }
        }

        // in case of miss:
        CacheBlock newBlock;
        newBlock.valid = true;
        newBlock.tag = tag;

        if (blocks.size() < associativity) {
            // Still have room in this set
            blocks.push_front(newBlock);
        } else {
            // Set is full. 
            // For BOTH LRU and FIFO, the block at the back is the one we want to evict.
            blocks.pop_back();
            blocks.push_front(newBlock);
        }
        return false;
    }
};

// 3. Cache Orchestrator (Breaks down addresses and routes to sets)
class Cache {
private:
    UINT32 numSets;
    UINT32 blockSize;
    UINT32 associativity;
    ReplacementPolicy policy;
    
    UINT32 offsetBits;
    UINT32 indexBits;
    UINT64 indexMask;
    
    std::vector<CacheSet> sets;

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

        offsetBits = calcLog2(blockSize);
        indexBits = calcLog2(numSets);
        indexMask = numSets - 1; 

        // Initialize sets with the chosen policy
        for (UINT32 i = 0; i < numSets; ++i) {
            sets.push_back(CacheSet(associativity, policy));
        }

        hits = misses = accesses = 0;
    }

    void access(UINT64 addr) {
        accesses++;
        
        UINT64 index = (addr >> offsetBits) & indexMask;
        UINT64 tag = addr >> (offsetBits + indexBits);

        if (sets[index].access(tag)) {
            hits++;
        } else {
            misses++;
        }
    }

    // Getters for final report
    UINT64 getHits() const { return hits; }
    UINT64 getMisses() const { return misses; }
    UINT64 getAccesses() const { return accesses; }
    double getHitRate() const { 
        return accesses == 0 ? 0.0 : (double)hits / accesses * 100.0; 
    }
    string getPolicyName() const {
        return (policy == LRU) ? "LRU" : "FIFO";
    }
};

// -------------------------------------------------------------------------
// PINTOOL INSTRUMENTATION
// -------------------------------------------------------------------------

static UINT64 icount = 0;
ADDRINT mainLow = 0, mainHigh = 0;

// Instantiate our cache simulator globally. 
Cache* dl1_cache; 

VOID docount() { icount++; }

// A single, unified memory access hook
VOID RecordMemAccess(VOID* addr) {
    dl1_cache->access((UINT64)addr);
}

VOID Instruction(INS ins, VOID* v) {
    // Only instrument instructions belonging to the main executable
    if (INS_Address(ins) <= mainHigh && INS_Address(ins) >= mainLow) {
        
        // ins count
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)docount, IARG_END);   

        // Iterate over every memory operand the instruction has
        UINT32 memOperands = INS_MemoryOperandCount(ins);
        for (UINT32 memOp = 0; memOp < memOperands; memOp++) {
            
            // If the operand is read from memory
            if (INS_MemoryOperandIsRead(ins, memOp)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess, 
                               IARG_MEMORYOP_EA, memOp, 
                               IARG_END);
            }
            
            // If the operand is written to memory
            // Note: An operand can be BOTH read and written (e.g., add [rax], 1)
            // This accurately simulates the two distinct cache accesses it requires.
            if (INS_MemoryOperandIsWritten(ins, memOp)) {
                INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RecordMemAccess, 
                               IARG_MEMORYOP_EA, memOp, 
                               IARG_END);
            }
        }
    }
}

KNOB< std::string > KnobInsCountFile(KNOB_MODE_WRITEONCE, "pintool", "i", "cache_stats.out", "specify output file name");

VOID Fini(INT32 code, VOID* v) {
    // Dump final Cache Statistics
    OutFile.setf(std::ios::showbase);
    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile << "L1 DATA CACHE SIMULATION RESULTS\n";
    OutFile << "-------------------------------------------------------------------------------\n";  
    
    OutFile << "Total Instructions  : " << icount << "\n";
    OutFile << "Total Mem Accesses  : " << dl1_cache->getAccesses() << "\n";
    OutFile << "Cache Hits          : " << dl1_cache->getHits() << "\n";
    OutFile << "Cache Misses        : " << dl1_cache->getMisses() << "\n";
    OutFile << "Hit Rate            : " << dl1_cache->getHitRate() << " %\n";
    
    OutFile << "-------------------------------------------------------------------------------\n";  
    OutFile.close();

    // Clean up memory
    delete dl1_cache;
}

INT32 Usage() {
    std::cerr << "This tool simulates a Data Cache." << std::endl;
    std::cerr << std::endl << KNOB_BASE::StringKnobSummary() << std::endl;
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

    // Initialize Cache: 32KB Size, 64-Byte Blocks, 8-way Associative, FIFO Policy
    dl1_cache = new Cache(32768, 64, 8, FIFO);

    OutFile.open(KnobInsCountFile.Value().c_str());

    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}
