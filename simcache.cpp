/*
CS-UY 2214
Written by Sanjida Orpi
Adapted from Jeff Epstein
E20 Cache Simulator
simcache.cpp
*/

#include <cstddef>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>
#include <fstream>
#include <iomanip>
#include <regex>
#include <cstdlib>
using namespace std;

// Some helpful constant values that we'll be using.
size_t const static NUM_REGS = 8;
size_t const static MEM_SIZE = 1<<13;
size_t const static REG_SIZE = 1<<16;

// Constant OpCode values to be used in Simulate function.
const uint16_t opAdd = 0;
const uint16_t opSub = 1;
const uint16_t opOr = 2;
const uint16_t opAnd = 3;
const uint16_t opSlt = 4;
const uint16_t opJr = 8;
const uint16_t opSlti = 57344;
const uint16_t opLw = 32768;
const uint16_t opSw = 40960;
const uint16_t opJeq = 49152;
const uint16_t opAddi = 8192;
const uint16_t opJ = 16384;
const uint16_t opJal = 24576;
// Constant values to compare intruction opCodes.
const uint16_t opMSB = 57344; // 3 MSB
const uint16_t opLSB = 15; // 4 LSB

// Value to track least recently used cache block
int cycle_counter = 0;

/*
    Loads an E20 machine code file into the list
    provided by mem. We assume that mem is
    large enough to hold the values in the machine
    code file.

    @param f Open file to read from
    @param mem Array represetnting memory into which to read program
*/
void load_machine_code(ifstream &f, unsigned mem[]) {
    regex machine_code_re("^ram\\[(\\d+)\\] = 16'b(\\d+);.*$");
    size_t expectedaddr = 0;
    string line;
    while (getline(f, line)) {
        smatch sm;
        if (!regex_match(line, sm, machine_code_re)) {
            cerr << "Can't parse line: " << line << endl;
            exit(1);
        }
        size_t addr = stoi(sm[1], nullptr, 10);
        unsigned instr = stoi(sm[2], nullptr, 2);
        if (addr != expectedaddr) {
            cerr << "Memory addresses encountered out of sequence: " << addr << endl;
            exit(1);
        }
        if (addr >= MEM_SIZE) {
            cerr << "Program too big for memory" << endl;
            exit(1);
        }
        expectedaddr ++;
        mem[addr] = instr;
    }
}

/*
    Prints out the correctly-formatted configuration of a cache.

    @param cache_name The name of the cache. "L1" or "L2"
    @param size The total size of the cache, measured in memory cells.
        Excludes metadata
    @param assoc The associativity of the cache. One of [1,2,4,8,16]
    @param blocksize The blocksize of the cache. One of [1,2,4,8,16,32,64])
    @param num_rows The number of rows in the given cache.
*/
void print_cache_config(const string &cache_name, int size, int assoc, int blocksize, int num_rows) {
    cout << "Cache " << cache_name << " has size " << size <<
        ", associativity " << assoc << ", blocksize " << blocksize <<
        ", rows " << num_rows << endl;
}

/*
    Prints out a correctly-formatted log entry.

    @param cache_name The name of the cache where the event
        occurred. "L1" or "L2"
    @param status The kind of cache event. "SW", "HIT", or
        "MISS"
    @param pc The program counter of the memory
        access instruction
    @param addr The memory address being accessed.
    @param row The cache row or set number where the data
        is stored.
*/
void print_log_entry(const string &cache_name, const string &status, int pc, int addr, int row) {
    cout << left << setw(8) << cache_name + " " + status <<  right <<
        " pc:" << setw(5) << pc <<
        "\taddr:" << setw(5) << addr <<
        "\trow:" << setw(4) << row << endl;
}

/*
    Creates a cache memory structure with an initial empty state. For memory 
    reads, if the desired cell is already in a cache, it will result in a hit,
    otherwise there is a miss.

    @params size, assoc, blocksize
*/
class Cache {
public:
    Cache(int size, int assoc, int blocksize) : size(size), assoc(assoc), blocksize(blocksize) {
        num_rows = size / (assoc * blocksize); // The number of rows in the cache
        for(int i = 0; i < num_rows; ++i) { // Initialize cache with the number of rows
            // Fill cache row with empty lru value and tag
            cache.push_back(vector<tuple<int,int>>(assoc, make_tuple(0,-1)));
        }
    }

    // Returns the row location of the current address 
    int get_row(int address) {
        int blockID = address / blocksize;
        return blockID % num_rows;
    }

    // Updates lru value to indicate tag is most recently used
    void update_lru(int addr, int index, int tag) { 
        for (int i = 0; i < assoc; ++i) {
            if (tag == get<1>(cache[index][i])) {
                get<0>(cache[index][i]) = cycle_counter;
            }
        }
    }

     // Returns the least recently used block index
    int get_lru(int addr, int index) {
        int least = get<0>(cache[index][0]);
        int least_ind = 0;
        for (int i = 0; i < assoc; ++i) {
            if (get<0>(cache[index][i]) < least) {
                least = get<0>(cache[index][i]);
                least_ind = i;
            }
        }
        return least_ind;
    }

    // Evicts a block if it was a miss, replaces the tag with the new tag
    void eviction(int addr, int index, int tag) {
        int empty_index = 0;
        if (assoc == 1) { // Direct-map cache: Update tag to new tag
            tuple<int,int> new_tup(0, tag);
            cache[index][0] = new_tup;
        } 
        
        else {
            int capacity = 0;
            for (int i = 0; i < assoc; ++i) { // Check if row is at full capacity
                if (get<1>(cache[index][i]) != -1) { // Not an empty tag
                    ++capacity;
                } else {
                    empty_index = i; // Index of a free space
                }
            }

            if (capacity != assoc) { // Cache row is not full
                tuple<int,int> new_tup(cycle_counter, tag);
                cache[index][empty_index] = new_tup;

            } else { // Eviction: replace the lru
                tuple<int,int> new_tup(cycle_counter, tag);
                cache[index][get_lru(addr, index)] = new_tup;
            }

            update_lru(addr, index, tag); // Update the memory as least recently used
        }
    }

    // Returns whether a memory access returns hit/miss, calls eviction if there is a miss
    bool cache_lookup(int addr) {
        int blockID = addr / blocksize;
        int index = blockID % num_rows;
        int tag = blockID / num_rows; 

        if (assoc == 1) { // Direct-map Cache
            if (get<1>(cache[index][0]) == tag) { // Check if tag matches the tag stored in the address row
                return true; // Hit
            }
        } 
        
        else { // Set/Fully Associative Cache
            for(int i = 0; i < assoc; ++i) {
                if(get<1>(cache[index][i]) == tag) {
                    update_lru(addr, index, tag);
                    return true; // Hit
                }
            }
        }

        eviction(addr, index, tag);
        return false; // MISS
    }

private:
    int size, assoc, blocksize, num_rows;
    vector<vector<tuple<int, int>>> cache; // Collection of rows which hold an lru data (0) and a tag (1)
};

/*
    Simulates one cache which accesses memory upon lw and sw instructions
    Updates L1 cache

    @param f Cache instance, address, opcode, pc
*/
void l1_cache(Cache& L1Cache, uint16_t address, uint16_t opCode, int pc) { // Only one (L1) cache is simulated.
    int row = L1Cache.get_row(address);
    if (opCode == opLw) {
        if (L1Cache.cache_lookup(address)) {
            print_log_entry("L1", "HIT", pc, address, row);

        } else {
            print_log_entry("L1", "MISS", pc, address, row);
        }

    } else if (opCode == opSw) {
        L1Cache.cache_lookup(address);
        print_log_entry("L1", "SW", pc, address, row);
    }
}

/*
    Simulates two caches which is accessed upon lw and sw instructions 
    Updates both L1 and L2 cache

    @param f Cache instance, Cache instance, address, opcode, pc
*/
void l1_l2_cache(Cache& L1Cache, Cache& L2Cache, uint16_t address, uint16_t opCode, int pc) { // Two caches (L1 and L2) simulated.
    int row1 = L1Cache.get_row(address);
    int row2 = L2Cache.get_row(address);
    bool L1_miss = false;

    if (opCode == opSw) { // sw
        L1Cache.cache_lookup(address); // Write through
        L2Cache.cache_lookup(address); // Write through

        print_log_entry("L1", "SW", pc, address, row1);
        print_log_entry("L2", "SW", pc, address, row2);

    } else { // lw
        if (L1Cache.cache_lookup(address)) {
            print_log_entry("L1", "HIT", pc, address, row1); // L1 HIT
            
        } else {
            L1_miss = true;
            print_log_entry("L1", "MISS", pc, address, row1); // L2 MISS
        }
    }

    if (L1_miss) { // Access L2 cache if there is an L1 cache miss
        if (L2Cache.cache_lookup(address)) { // Check L2 Cache
            print_log_entry("L2", "HIT", pc, address, row2); // L2 HIT

        } else {
            print_log_entry("L2", "MISS", pc, address, row2); // L2 MISS
            L1Cache.cache_lookup(address); // Save to L1
        }
    }
}

/*
    This function is called from the Simulate function, it handles
    instructions with three register arguments which save a value to
    a mutable destination register and update program counter
    @param memory Array of memory values
    @param reg Array of register values
    @param pc Program counter
*/
void threeRegInstr(unsigned mem[], uint16_t reg[], uint16_t& pc) {
    uint16_t intruction = mem[pc & 8191];
    uint16_t opCode = intruction & opLSB;
    uint16_t regA = ((intruction & 7168) >> 10);
    uint16_t regB = ((intruction & 896) >> 7);
    uint16_t regDST = ((intruction & 112) >> 4);

    if (opCode == opAdd) { // Addi: Adds the value of regA and regB.
        reg[regDST] = reg[regA] + reg[regB];
        ++pc;

    } else if (opCode == opSub) { // Sub: Subtracts the value of register regB from regA.
        reg[regDST] = reg[regA] - reg[regB];
        ++pc;

    } else if (opCode == opOr) { // Or: Bitwise OR of the value of registers regA and regB.
        reg[regDST] = reg[regA] | reg[regB]; 
        ++pc;

    } else if (opCode == opAnd) { // AND: Bitwise AND of the value of registers regA and regB.
        reg[regDST] = reg[regA] & reg[regB];
        ++pc;

    } else if (opCode == opSlt) { // Slt: Compares the value of regA with regB.
        if (reg[regA] < reg[regB]) {
            reg[regDST] = 1;
        } else {
            reg[regDST] = 0;
        }
        ++pc;

    } else if (opCode == opJr) { // Jr: Jumps unconditionally to the memory address in reg.
        pc = reg[regA];
    }
    reg[0] = 0;
}

/* 
    This function is called from the Simulate function, it handles 
    instructions with two register arguments and updates program counter,
    bits 10-12 are represented by regA and bits 7-9 are represented by regB

    @param memory Array of memory values
    @param reg Array of register values
    @param pc Program counter
*/
void twoRegInstr(unsigned mem[], uint16_t reg[], uint16_t& pc, unsigned opCode, int cache_size, vector<Cache>& caches) {
    uint16_t instruction = mem[pc & 8191];
    uint16_t immVal = instruction & 127; // Slice the 7 bit imm
    uint16_t regA = ((instruction & 7168) >> 10);
    uint16_t regB = ((instruction & 896) >> 7);
    if (immVal & 64) { // MSB of the 7 bit imm is 1, sign extend.
        immVal |= 65408;
    }

    if (opCode == opSlti) { // Slti: Compares value of regA (SRC) with sign-extended imm.
        if (reg[regA] < immVal) {
            reg[regB] = 1;
        } else {
            reg[regB] = 0;
        }
        ++pc;

    } else if (opCode == opJeq) { // Jeq: Compares the value of regA with regB.
        if (reg[regA] == reg[regB]) {
            pc += immVal + 1; // rel_imm
            pc >= 128 ? pc %= 128 : pc = pc; // PC wraps around if it exceeds the range of valid addresses.
        } else {
            ++pc;
        }

    } else if (opCode == opAddi) { // Addi: Adds the value of register regA (SRC) and the signed number immVal.
        reg[regB] = reg[regA] + immVal;
        ++pc;

    } else {
        // LW & SW access cache
        uint16_t addr = (immVal + reg[regA]) & 8191; // 13 bit address to index memory.
        if (opCode == opLw) { // Lw: Calculates a memory pointer (signed imm + regA (Addr)), and loads value at address.
            reg[regB] = mem[addr];
            
        } else if (opCode == opSw) { // Sw: Calculates a memory pointer (signed imm + regA (Addr)), and stores value.
            mem[addr & 8191] = reg[regB];
        }

        reg[0] = 0;
        if (cache_size == 1) { // Make cache call: Simulate one cache
            l1_cache(caches[0], addr, opCode, pc);
        } else if (cache_size == 2) { // Make cache call: Simulate two caches
            l1_l2_cache(caches[0], caches[1], addr, opCode, pc);
        }

        ++pc;
    }
    reg[0] = 0;
}

/* 
    Simulates valid E20 machine code, categorized by instruction format 
    (based on the number of register arguments or instruction). It manipulates
    the program counter, the general-purpose registers, and memory

    @param memory Array of memory values
    @param reg Array of register values
    @param pc Program counter
*/
void simulate(unsigned mem[], uint16_t reg[], uint16_t& pc, int cache_size, vector<Cache>& caches) {
    while(pc < REG_SIZE) { // Program counter ranges from memory cell 0 to 8191.
        uint16_t instruction = mem[pc & 8191];
        ++cycle_counter;

        if ((instruction & opMSB) == 0) { // The opCode is in the 4 LSB.
            threeRegInstr(mem, reg, pc);

        } else { // The opCode is in the 3 MSB.
            uint16_t opCode = instruction & opMSB;
            uint16_t immVal = instruction & 8191; // Instructions have a 13 bit imm value.
            if (opCode == opJal) { // Jal
                reg[7] = pc + 1;
                pc = immVal;

            } else if ((opCode == opJ) & (immVal == pc)) {
                return; // Halt instruction, stop simulating.

            } else if ((opCode == opJ) & (immVal != pc)) {
                pc = immVal;

            } else { // Instructions have a 7 bit imm value.
                twoRegInstr(mem, reg, pc, opCode, cache_size, caches);
            }
        }

        if (pc > (REG_SIZE-1)) { 
            // PC wraps around when it exceeds the last cell.
            pc %= (REG_SIZE-1);
        }
    }
}

/**
    Main function
    Takes command-line args as documented below
*/
int main(int argc, char *argv[]) {
    /*
        Parse the command-line arguments
    */
    char *filename = nullptr;
    bool do_help = false;
    bool arg_error = false;
    string cache_config;
    for (int i=1; i<argc; i++) {
        string arg(argv[i]);
        if (arg.rfind("-",0)==0) {
            if (arg== "-h" || arg == "--help")
                do_help = true;
            else if (arg=="--cache") {
                i++;
                if (i>=argc)
                    arg_error = true;
                else
                    cache_config = argv[i];
            }
            else
                arg_error = true;
        } else {
            if (filename == nullptr)
                filename = argv[i];
            else
                arg_error = true;
        }
    }
    /* Display error message if appropriate */
    if (arg_error || do_help || filename == nullptr) {
        cerr << "usage " << argv[0] << " [-h] [--cache CACHE] filename" << endl << endl;
        cerr << "Simulate E20 cache" << endl << endl;
        cerr << "positional arguments:" << endl;
        cerr << "  filename    The file containing machine code, typically with .bin suffix" << endl<<endl;
        cerr << "optional arguments:"<<endl;
        cerr << "  -h, --help  show this help message and exit"<<endl;
        cerr << "  --cache CACHE  Cache configuration: size,associativity,blocksize (for one"<<endl;
        cerr << "                 cache) or"<<endl;
        cerr << "                 size,associativity,blocksize,size,associativity,blocksize"<<endl;
        cerr << "                 (for two caches)"<<endl;
        return 1;
    }

    // Load f and parse using load_machine_code.
    ifstream f(filename);
    if (!f.is_open()) {
        cerr << "Can't open file "<<filename<<endl;
        return 1;
    }
    // Initialize memory and registers for simulation.
    unsigned memory[MEM_SIZE] = {0};
    load_machine_code(f, memory);
    uint16_t reg[NUM_REGS] = {0};
    uint16_t pc = 0;

    /* parse cache config */
    if (cache_config.size() > 0) {
        vector<int> parts;
        size_t pos;
        size_t lastpos = 0;
        while ((pos = cache_config.find(",", lastpos)) != string::npos) {
            parts.push_back(stoi(cache_config.substr(lastpos,pos)));
            lastpos = pos + 1;
        }
        parts.push_back(stoi(cache_config.substr(lastpos)));
        if (parts.size() == 3) {
            int L1size = parts[0];
            int L1assoc = parts[1];
            int L1blocksize = parts[2];

            int rows = L1size / (L1assoc * L1blocksize);
            print_cache_config("L1" , L1size, L1assoc , L1blocksize, rows);
            int cache_size = 1;

            // Make an L1 cache instance
            Cache L1Cache(L1size, L1assoc, L1blocksize);
            vector<Cache> caches = {L1Cache};

            // Call Simulate function to update memory, registers, and final program counter.
            simulate(memory, reg, pc, cache_size, caches);

        } else if (parts.size() == 6) {
            int L1size = parts[0];
            int L1assoc = parts[1];
            int L1blocksize = parts[2];
            int L2size = parts[3];
            int L2assoc = parts[4];
            int L2blocksize = parts[5];

            int rows1 = L1size / (L1assoc * L1blocksize);
            int rows2 = L2size / (L2assoc * L2blocksize);

            print_cache_config("L1" , L1size, L1assoc , L1blocksize, rows1);
            print_cache_config("L2" , L2size, L2assoc , L2blocksize, rows2);
            
            int cache_size = 2;

            // Make an L1 and L2 cache instance
            Cache L1Cache(L1size, L1assoc, L1blocksize);
            Cache L2Cache(L2size, L2assoc, L2blocksize);
            vector<Cache> caches = {L1Cache, L2Cache};

            // Call Simulate function to update memory, registers, and final program counter.
            simulate(memory, reg, pc, cache_size, caches);
            
        } else {
            cerr << "Invalid cache config"  << endl;
            return 1;
        }
    }

    return 0;
}
//ra0Eequ6ucie6Jei0koh6phishohm9
