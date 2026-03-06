# Parallel-Firewall

A multithreaded network firewall simulator implemented in **C**, designed to process
data packets in parallel using the **Producer-Consumer design pattern** and **POSIX Threads (pthreads)**.

## Overview
This project simulates a real-world firewall setup where a high-speed network interface receives packets
that must be filtered (accepted or dropped) based on specific security rules. To handle high
traffic volumes, the engine utilizes a thread-safe circular buffer to distribute processing
tasks across multiple consumer threads.

## Technical Architecture
The system is built on three core pillars of concurrent programming:

1. **Producer-Consumer Pattern:**
   - **Producer:** Simulates a network card, generating and injecting packets into the system.
   - **Consumers:** Multiple firewall worker threads that process packets, apply filtering
   logic, and log decisions.
2. **Synchronized Ring Buffer:**
   - A custom-built, fixed-size circular buffer that manages data flow between threads.
   - **Synchronization:** Implemented using **Mutexes** and **Condition Variables**
   to ensure thread safety and eliminate race conditions.
3. **Non-Blocking Synchronization:**
   - Threads are suspended when the buffer is empty or
   full and are notified via signals, ensuring **zero busy-waiting**.



## Key Features & Challenges

### Advanced Multithreading (Pthreads)
- Efficiently manages lifecycle and synchronization for `N` consumer threads.
- Implements thread-safe exit sequences when the producer closes the connection.

### Ordered Logging (Concurrency Challenge)
- **Problem:** Parallel threads finish tasks in a non-deterministic order, but
logs must remain sorted by the packet's original timestamp.
- **Solution:** Implemented a synchronization mechanism that allows threads to
write to the log file in the correct chronological order without post-processing or
sorting the file after execution.


## Setup & Execution

### Prerequisites
- Linux-based environment
- GCC Compiler & Make
- Libraries: pthreads (POSIX Threads)

### Building the project
```
cd src
make
```

### Testing the project
```
cd tests/
make check
```
