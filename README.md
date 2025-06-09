CS-UY 2214 (Computer Architecture) Project - Cache Simulator (C++)

This project implements a cache simulator for the E20 instruction set architecture (ISA), designed as a project for Computer Architecture. The simulator builds on an existing E20 CPU emulator and models the behavior of memory caching mechanisms during program execution.
The E20 Cache Simulator simulates one or two levels of cache (L1 and optional L2), tracking hits, misses, and writes as a .bin machine code program executes. Each cache is configurable by size, associativity, and block size, and uses a Least Recently Used (LRU) replacement policy. The simulation follows a write-through with write-allocate (WTWA) strategy for all writes.

💡 Features
  - Support for L1-only or L1+L2 cache configurations
  - Tracks all memory reads (lw) and writes (sw)
  - Uses LRU for associative cache eviction
  - Produces detailed, chronologically ordered logs for cache events
  - Handles cache initialization and E20 .bin input formats
  - Configurable via command-line --cache flag

⚙️ Cache Configuration - Each cache is described with:
  - Size: Total number of memory cells (bytes) in the cache
  - Associativity: Number of blocks per set (1 = direct-mapped, >1 = set-associative)
  - Blocksize: Number of memory cells per block
  - L1 or both L1 and L2 caches
