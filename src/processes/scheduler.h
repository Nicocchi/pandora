#pragma once

#include <stdint.h>
#include <stddef.h>
#include "memory/vmm.h"
#include "memory/heap.h"
#include "common.h"

// Kernel stack size per task
static constexpr size_t TASK_KERNEL_STACK_SIZE = 16384; // 16 KiB

enum class TaskPriority : uint8_t
{
    Idle = 0,       // Only runs when nothing else can
    Low = 1,
    Normal = 2,     // Defaut for kernel threads and user processes
    High = 3,
    Realtime = 4    // Reserved for kernel subsystems
};

// Time slice in PIT ticks per priority level (100Hz PIT = 10ms per tick)
static constexpr uint32_t PRIORITY_TICKS[5] =
{
    1,  // Idle     - 10ms
    2,  // Low      - 20ms
    4,  // Normal   - 40ms
    6,  // High     - 60ms
    8   // Realtime - 80ms
};

// Task state
enum class TaskState : uint8_t
{
    Created,    // Allocated, not yet runnable
    Running,    // Currently executing
    Ready,      // In run queue, waiting for CPU
    Blocked,    // Waiting for event (sleep, I/O, mutex)
    Zombie,     // Finished, waiting for parent to reap
    Dead        // Fully cleaned up
};

struct TaskContext
{
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
};

struct Task;

// Process - Owns an address space, a PID, and a list of threads
struct Process
{
    uint32_t pid;
    AddressSpace address_space;
    Task *threads;
    uint32_t thread_count;
    bool is_kernel_process;
    char cwd[256];

    static Process *CreateKernelProcess();
    static Process *CreateUserProcess();
    void Destroy();
};

// Task (thread) - owns a kernel stack, register context, and schedule state
struct Task
{
    // Identity
    uint32_t tid;
    char name[32];
    Process *process;

    // CPU state
    TaskContext context;

    // Stack
    uint8_t *kernel_stack;
    uint8_t *kernel_stack_top;
    uint64_t user_stack_top;

    // Scheduling
    TaskState state;
    TaskPriority priority;
    uint32_t ticks_remaining;
    uint64_t total_ticks;
    uint64_t sleep_until;

    // Linked list (Run queue / thread list)
    Task *next;
    Task *prev;

    // Thread entry point (for initial setup only)
    void (*entry)(void *arg);
    void *arg;

    // Process hierarchy (fork/wait). Single foreground child is enough for the
    // shell use-case; generalize to a child list later if needed.
    Task *parent;       // task to notify on exit (set at fork)
    Task *child;        // most recent child created via fork
    long exit_code;     // value passed to exit(); valid once `exited` is true
    bool exited;        // true once this task has called Exit()
    bool waiting;       // true while blocked inside wait()
};

// Scheduler
struct Scheduler
{
    Task *queues[5];
    Task *sleep_queue;
    uint32_t queue_sizes[5];

    Task *current;
    Task *idle_task;
    uint64_t tick_count;
    bool need_resched;

    void Init();
    void Schedule();
    void Enqueue(Task *task);
    void Dequeue(Task *task);
    void Tick();
    void Yield();
    void Sleep(uint64_t ticks);
    void Exit(long code = 0);
    Task *PickNext(bool skip_current = false);
    void SwitchTo(Task *next);
    void Block(Task* waiter); // block current task
    void Wake(Task* task);  // wake a specific task

    // Block the current task until its child exits; returns the child's exit
    // code and reaps the child's resources. Returns -1 if there is no child.
    long Wait();

    // Free a finished (Zombie) child's address space, kernel stack and structs.
    void Reap(Task* child);

    // Fork the current user task: deep-copy its address space and build a child
    // task that returns to the same user RIP with rax=0. Returns the child Task
    // (whose process->pid is the fork return value), or nullptr on failure.
    Task* Fork(struct SyscallFrame* frame);
};

extern Scheduler scheduler;

Task *KThreadCreate(const char *name, void (*entry)(void*), void *arg, 
                    TaskPriority priority = TaskPriority::Normal);

Task *UThreadCreate(Process *proc, uint64_t entry_virt, uint64_t user_stack,
                    TaskPriority priority = TaskPriority::Normal);

inline void KYield()
{
    scheduler.Yield();
}

inline void KSleep(uint32_t ms)
{
    uint64_t ticks = (ms + 9) / 10; // round up; PIT is 100 Hz
    if (ticks == 0) ticks = 1;
    scheduler.Sleep(ticks);
}

extern "C" void context_switch(TaskContext *old_ctx, TaskContext *new_ctx);

extern "C" void tss_set_rsp0(uint64_t rsp0);