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


enum ReplacementPolicy {
    LRU,
    FIFO,
    LFU,
    RAN,
    DYN
};

// --- COMMAND LINE CONFIGURATION KNOBS ---
KNOB<UINT32> KnobL1Size(KNOB_MODE_WRITEONCE, "pintool", "l1_size", "32768", "L1 cache size in bytes");
KNOB<UINT32> KnobL1Assoc(KNOB_MODE_WRITEONCE, "pintool", "l1_assoc", "8", "L1 cache associativity");
KNOB<UINT32> KnobL1Block(KNOB_MODE_WRITEONCE, "pintool", "l1_block", "64", "L1 cache block size");
KNOB<string> KnobL1Policy(KNOB_MODE_WRITEONCE, "pintool", "l1_policy", "LRU", "L1 policy (LRU, FIFO, LFU, RAN, DYN)");

KNOB<UINT32> KnobL2Size(KNOB_MODE_WRITEONCE, "pintool", "l2_size", "262144", "L2 cache size in bytes");
KNOB<UINT32> KnobL2Assoc(KNOB_MODE_WRITEONCE, "pintool", "l2_assoc", "16", "L2 cache associativity");
KNOB<UINT32> KnobL2Block(KNOB_MODE_WRITEONCE, "pintool", "l2_block", "64", "L2 cache block size");
KNOB<string> KnobL2Policy(KNOB_MODE_WRITEONCE, "pintool", "l2_policy", "LRU", "L2 policy (LRU, FIFO, LFU, RAN, DYN)");

ReplacementPolicy parsePolicy(const string& pol) {
    if (pol == "FIFO") return FIFO;
    if (pol == "LFU") return LFU;
    if (pol == "RAN") return RAN;
    if (pol == "DYN") return DYN;
    return LRU; // Default
}

class CacheSet {
private:
    UINT32 associativity;
    list<CacheBlock> blocks; 
    std::mt19937 gen;
    std::uniform_int_distribution<int> dist;

public:
    CacheSet(UINT32 assoc) : associativity(assoc), gen(random_device{}()), dist(0, assoc - 1) {}

    // activePolicy is passed per-access
    bool access(UINT64 tag, bool isWrite, bool& evictedDirty, UINT64& evictedTag, ReplacementPolicy activePolicy) {
        evictedDirty = false; 

        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->valid && it->tag == tag) {
                if (isWrite) it->dirty = true; 

                if (activePolicy == LFU || activePolicy == FIFO || activePolicy == RAN ){
                    it->freq++;
                }
                else if (activePolicy == LRU) {
                    CacheBlock hitBlock = *it;
                    hitBlock.freq++;
                    blocks.erase(it);
                    blocks.push_front(hitBlock);
                }
                return true; 
            }
        }

        // MISS HANDLING
        CacheBlock newBlock;
        newBlock.valid = true;
        newBlock.dirty = isWrite; 
        newBlock.tag = tag;
        newBlock.freq = 1;

        if (blocks.size() < associativity) {
            blocks.push_front(newBlock);
        } else {
            auto victim = blocks.end();

            // Eviction logic uses activePolicy
            if (activePolicy == LRU || activePolicy == FIFO) {
                victim = std::prev(blocks.end()); 
            }
            else if (activePolicy == LFU) {
                victim = blocks.begin();
                for (auto it = blocks.begin(); it != blocks.end(); ++it) {
                    if (it->freq < victim->freq) victim = it;
                }
            }
            else if (activePolicy == RAN) {
                int idx = dist(gen);
                victim = blocks.begin();
                advance(victim, idx); 
            }

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


class Cache {
private:
    UINT32 numSets;
    UINT32 blockSize;
    UINT32 associativity;
    ReplacementPolicy policy;
    vector<CacheSet> sets;

    // Policy Selector for Set Dueling (DYN policy)
    // psel > 0 means RAN is better, psel < 0 means LRU is better
    int psel; 

    UINT64 hits;
    UINT64 misses;
    UINT64 accesses;

public:
    Cache(UINT32 sizeBytes, UINT32 bSize, UINT32 assoc, ReplacementPolicy pol) {
        blockSize = bSize;
        associativity = assoc;
        policy = pol;
        numSets = sizeBytes / (blockSize * associativity);
        psel = 0; // Initialize saturating counter

        for (UINT32 i = 0; i < numSets; ++i) {
            sets.push_back(CacheSet(associativity)); // Removed fixed policy
        }
        hits = misses = accesses = 0;
    }

    bool access(UINT64 addr, bool isWrite, bool& evictWB, UINT64& wbAddr) {
        accesses++;
        
        UINT64 blockAddress = addr / blockSize; 
        UINT64 index = blockAddress % numSets;  
        UINT64 tag = blockAddress / numSets;    

        bool evictedDirty = false;
        UINT64 evictedTag = 0;

        // --- SET DUELING LOGIC ---
        ReplacementPolicy activePolicy = policy;
        if (policy == DYN) {
            if (index % 32 == 0) {
                activePolicy = LRU; // Leader 0: Always uses LRU
            } else if (index % 32 == 1) {
                activePolicy = RAN; // Leader 1: Always uses Random
            } else {
                // Followers use the winner of the duel
                activePolicy = (psel >= 0) ? RAN : LRU; 
            }
        }

        // Pass activePolicy to the set
        bool hit = sets[index].access(tag, isWrite, evictedDirty, evictedTag, activePolicy);
        
        // Update Policy Selector on a miss
        if (!hit && policy == DYN) {
            if (index % 32 == 0) {
                psel++; // LRU missed, so favor RAN
                if (psel > 511) psel = 511; // 10-bit saturating counter upper bound
            } else if (index % 32 == 1) {
                psel--; // RAN missed, so favor LRU
                if (psel < -512) psel = -512; // 10-bit saturating counter lower bound
            }
        }

        if (evictedDirty) {
            evictWB = true;
            wbAddr = ((evictedTag * numSets) + index) * blockSize;
        } else {
            evictWB = false;
        }

        if (hit) hits++;
        else misses++;
        
        return hit;
    }
    // getter functions for good oop access
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
        if (policy == DYN) return "DYNAMIC (SET DUELING)";
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

VOID RecordMemAccess(VOID* addr, bool isWrite) {
    bool evictWB = false;
    UINT64 wbAddr = 0;

    if (!dl1_cache->access((UINT64)addr, isWrite, evictWB, wbAddr)) {
        // L1 miss, try L2
        bool dummyWB; UINT64 dummyAddr;
        dl2_cache->access((UINT64)addr, false, dummyWB, dummyAddr);
    }

    // Did L1 kick out a dirty block?
    if (evictWB) {
        // Write the modified block down to L2
        bool dummyWB; UINT64 dummyAddr;
        dl2_cache->access(wbAddr, true, dummyWB, dummyAddr);
    }

}

VOID Instruction(INS ins, VOID* v) {
    if (INS_Address(ins) <= mainHigh && INS_Address(ins) >= mainLow) {
        
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
        }
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
    int L1_LATENCY = 1;     
    int L2_LATENCY = 10;    
    int MEM_LATENCY = 100;  
    
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
    // PIN_Init returns true if parsing fails or user asks for help
    if (PIN_Init(argc, argv)) return Usage();

    // Initialize Caches dynamically
    dl1_cache = new Cache(KnobL1Size.Value(), KnobL1Block.Value(), KnobL1Assoc.Value(), parsePolicy(KnobL1Policy.Value()));
    dl2_cache = new Cache(KnobL2Size.Value(), KnobL2Block.Value(), KnobL2Assoc.Value(), parsePolicy(KnobL2Policy.Value()));

    OutFile.open(KnobInsCountFile.Value().c_str());

    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    return 0;
}
