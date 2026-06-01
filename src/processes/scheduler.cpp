#include "scheduler.h"
#include "lib/string.h"
#include "gdt.h"

Scheduler scheduler;

// Global TID counters
static uint32_t s_next_tid = 1;
static uint32_t s_next_pid = 1;

extern TSSEntry tss;
// extern "C" uint64_t *tss_rsp0_ptr = &tss.rsp0;
extern "C" void user_entry_iretq();

static void StiIfInterruptsDisabled()
{
    uint64_t flags;
    asm volatile("pushfq; pop %0" : "=r"(flags) : : "memory");
    if (!(flags & (1ULL << 9)))
    {
        asm volatile("sti" ::: "memory");
    }
}

static void task_entry_stub()
{
    asm volatile("sti");

    Task *self = scheduler.current;
    if (self && self->entry)
    {
        self->entry(self->arg);
    }

    scheduler.Exit();

    // Should never reach here
    hcf();
}

Process *Process::CreateKernelProcess()
{
    Process *p = knew<Process>();
    p->pid = s_next_pid++;
    p->is_kernel_process = true;
    p->threads = nullptr;
    p->thread_count = 0;
    // Shares the global kernel address space - no Init() needed
    return p;
}

Process *Process::CreateUserProcess()
{
    Process *p = knew<Process>();
    p->pid = s_next_pid++;
    p->is_kernel_process = false;
    p->threads = nullptr;
    p->thread_count = 0;
    // Fork kernel address space so upper-half mappings are inherited
    p->address_space.Init(&virtualMemoryManager.kernel_space);
    return p;
}

void Process::Destroy()
{
    // TODO: free user pages, unmap address space free Task structs
    kfree(this);
}

static Task *AllocTask(const char *name, void (*entry)(void *), void *arg, 
                        TaskPriority priority, Process *proc)
{
    Task *t = knew<Task>();

    t->tid = s_next_tid++;
    t->process = proc;
    t->entry = entry;
    t->arg = arg;
    t->state = TaskState::Created;
    t->priority = priority;
    t->ticks_remaining = PRIORITY_TICKS[(uint8_t)priority];
    t->total_ticks = 0;
    t->sleep_until = 0;
    t->next = nullptr;
    t->prev = nullptr;
    t->user_stack_top = 0;

    // Copy name
    size_t nlen = 0;
    while (name[nlen] && nlen < 31) nlen++;
    for (size_t i = 0; i < nlen; i++) t->name[i] = name[i];
    t->name[nlen] = '\0';

    // Allocate kernel stack
    t->kernel_stack = (uint8_t*)kmalloc(TASK_KERNEL_STACK_SIZE);
    t->kernel_stack_top = t->kernel_stack + TASK_KERNEL_STACK_SIZE;

    // Set up initial kernel stack frame
    // Fake a stack taht looks like a context_switch() was called and
    // saved state, so when context_switch() restores this task it will
    // "return" into task_entry_stub.
    //
    // Stack grows downward. Push one value: the stub's address.
    // context_switch() restores RSP to this location, then `ret` pops it
    uint64_t *stack_top = (uint64_t*)t->kernel_stack_top;
    stack_top--;
    *stack_top = (uint64_t)task_entry_stub;

    // All callee-saved regs start as 0
    t->context.rbx = 0;
    t->context.rbp = 0;
    t->context.r12 = 0;
    t->context.r13 = 0;
    t->context.r14 = 0;
    t->context.r15 = 0;
    t->context.rsp = (uint64_t)stack_top; // RSP points at stub address

    return t;
}

static Process *s_kernel_proc = nullptr;
Task *KThreadCreate(const char *name, void (*entry)(void*), void *arg, TaskPriority priority)
{
    // All kernel threads belong to the single kernel process
    if (!s_kernel_proc)
    {
        s_kernel_proc = Process::CreateKernelProcess();
    }

    Task *t = AllocTask(name, entry, arg, priority, s_kernel_proc);
    t->state = TaskState::Ready;

    scheduler.Enqueue(t);

    return t;
}

extern "C" void user_entry_stub();

__attribute__((naked)) void user_entry_stub()
{
    asm volatile(
        "pop %r15\n"
        "pop %r14\n"
        "pop %r13\n"
        "pop %r12\n"
        "pop %r11\n"
        "pop %r10\n"
        "pop %r9\n"
        "pop %r8\n"
        "pop %rbp\n"
        "pop %rdi\n"
        "pop %rsi\n"
        "pop %rdx\n"
        "pop %rcx\n"
        "pop %rbx\n"
        "pop %rax\n"
        "iretq\n"
    );
}

Task *UThreadCreate(Process *proc, uint64_t entry_virt, uint64_t user_stack,
                    TaskPriority priority)
{
    Task *t = AllocTask("uthread", nullptr, nullptr, priority, proc);
    t->user_stack_top = user_stack;

    uint64_t *stack_top = (uint64_t*)t->kernel_stack_top;

    // iretq frame (pushed in reverse - stack grows down)
    *--stack_top = GDT_USER_DATA;   // SS
    *--stack_top = user_stack;      // RSP (user stack)
    *--stack_top = 0x202;           // RFLAGS: IF=1, reserved bit 1
    *--stack_top = GDT_USER_CODE;   // CS
    *--stack_top = entry_virt;      // RIP (user entry point)

    *--stack_top = (uint64_t)user_entry_iretq;

    t->context.rsp = (uint64_t)stack_top;
    t->state = TaskState::Ready;

    if (proc) {
        // Link into process thread list
        t->next = proc->threads;
        if (proc->threads) proc->threads->prev = t;
        proc->threads = t;
        proc->thread_count++;
    }

    scheduler.Enqueue(t);
    return t;
}

void Scheduler::Init()
{
    kprintf("Scheduler Init\n");
    for (int i = 0; i < 5; i++)
    {
        queues[i] = nullptr;
        queue_sizes[i] = 0;
    }
    current = nullptr;
    sleep_queue = nullptr;
    tick_count = 0;

    // Create the idle task - runs when nothing else is ready
    // Idle just halts until the next interrupt
    idle_task = AllocTask("idle", [](void*) {
        for (;;) asm volatile("hlt");
    }, nullptr, TaskPriority::Idle, nullptr);
    idle_task->state = TaskState::Ready;
    Enqueue(idle_task);

    // The bootstrap task (kmain) needs a Task struct so the first
    // context_switch() has somewhere to save state.
    // Create it as already running with no entry function
    Task *bootstrap = knew<Task>();
    bootstrap->tid = 0;
    bootstrap->process = nullptr;
    bootstrap->entry = nullptr;
    bootstrap->arg = nullptr;
    bootstrap->state = TaskState::Running;
    bootstrap->priority = TaskPriority::Idle;
    bootstrap->ticks_remaining = PRIORITY_TICKS[(uint8_t)TaskPriority::Idle];
    bootstrap->total_ticks = 0;
    bootstrap->sleep_until = 0;
    bootstrap->next = nullptr;
    bootstrap->prev = nullptr;
    bootstrap->kernel_stack = nullptr; // Uses existing kmain stack
    uint64_t bootstrap_rsp = 0;
    asm volatile("mov %%rsp, %0" : "=r"(bootstrap_rsp));
    bootstrap->kernel_stack_top = (uint8_t*)bootstrap_rsp;
    bootstrap->name[0] = 'k'; bootstrap->name[1] = 'm';
    bootstrap->name[2] = 'a'; bootstrap->name[3] = 'i';
    bootstrap->name[4] = 'n'; bootstrap->name[5] = '\0';

    current = bootstrap;
    need_resched = false;
}

void Scheduler::Enqueue(Task *task)
{
    uint8_t p = (uint8_t)task->priority;
    task->state = TaskState::Ready;

    if (!queues[p])
    {
        queues[p] = task;
        task->next = task;
        task->prev = task;
    }
    else
    {
        Task *head = queues[p];
        Task *tail = head->prev;
        tail->next = task;
        task->prev = tail;
        task->next = head;
        head->prev = task;
    }
    queue_sizes[p]++;
}

void Scheduler::Dequeue(Task *task)
{
    uint8_t p = (uint8_t)task->priority;
    if (queue_sizes[p] == 0) return;

    // Running tasks are removed by PickNext(); only queued tasks have valid links.
    if (!task->next || !task->prev) return;

    if (task->next == task)
    {
        // Only element
        queues[p] = nullptr;
        queue_sizes[p]--;
    }
    else
    {
        task->prev->next = task->next;
        task->next->prev = task->prev;
        if (queues[p] == task)
        {
            queues[p] = task->next;
        }
        task->next = nullptr;
        task->prev = nullptr;
        queue_sizes[p]--;
    }
}

Task *Scheduler::PickNext()
{
    // Walk from highest priority to lowest
    for (int p = 4; p >= 0; p--)
    {
        if (queues[p])
        {
            Task *t = queues[p];
            Dequeue(t);
            return t;
        }
    }
    return idle_task; // always available
}

void Scheduler::SwitchTo(Task *next_task)
{
    if (next_task == current) return;

    Task *prev = current;
    current = next_task;
    next_task->state = TaskState::Running;
    next_task->ticks_remaining = PRIORITY_TICKS[(uint8_t)next_task->priority];

    // Update TSS rsp0 so Ring 3 -> Ring 0 transitions land on the right stack
    if (next_task->kernel_stack_top)
    {
        tss.rsp0 = (uint64_t)next_task->kernel_stack_top;
    }
    // tss_set_rsp0((uint64_t)next_task->kernel_stack_top);

    // Load new address space if switching processes
    if (next_task->process &&
        prev->process != next_task->process &&
        !next_task->process->is_kernel_process)
    {
        next_task->process->address_space.Load();
    }

    context_switch(&prev->context, &next_task->context);
}

void Scheduler::Tick()
{
    tick_count++;

    // Wake sleeping tasks from sleep queue
    bool woke = false;
    Task *prev_sleep = nullptr;
    Task *t = sleep_queue;
    while (t != nullptr)
    {
        Task *next_t = t->next;
        if (tick_count >= t->sleep_until)
        {
            if (prev_sleep) prev_sleep->next = next_t;
            else            sleep_queue       = next_t;

            t->next = nullptr;
            t->prev = nullptr;
            Enqueue(t);
            woke = true;
        }
        else
        {
            prev_sleep = t;
        }
        t = next_t;
    }

    if (!current) return;

    // If a task woke while idle is running, reschedule on IRQ exit (not here).
    if (woke && current == idle_task)
    {
        current->ticks_remaining = 0;
    }

    current->total_ticks++;
    if (current->ticks_remaining > 0) current->ticks_remaining--;

    if (current->ticks_remaining == 0)
    {
        need_resched = true;
    }
}

void Scheduler::Schedule()
{
    if (!need_resched || !current) return;
    if (current->ticks_remaining > 0) return;

    need_resched = false;

    Task *next_task = PickNext();
    if (next_task == current) return;

    if (current->state == TaskState::Running)
    {
        current->state = TaskState::Ready;
        if (current != idle_task && current->entry != nullptr)
        {
            Enqueue(current);
        }
    }

    asm volatile("cli");
    SwitchTo(next_task);
}

void Scheduler::Sleep(uint64_t ticks)
{
    asm volatile("cli");

    if (ticks == 0) ticks = 1;

    current->state = TaskState::Blocked;
    current->sleep_until = tick_count + ticks;

    // Already off the run queue while Running (PickNext dequeued us).
    current->next = sleep_queue;
    current->prev = nullptr;
    sleep_queue = current;

    Task *next_task = PickNext();
    SwitchTo(next_task);

    StiIfInterruptsDisabled();
}

void Scheduler::Yield()
{
    asm volatile("cli");

    if (current->state == TaskState::Running)
    {
        current->state = TaskState::Ready;
        if (current != idle_task && current->entry != nullptr)
        {
            Enqueue(current);
        }
    }

    Task *next_task = PickNext();
    if (next_task != current)
    {
        SwitchTo(next_task);
    }

    StiIfInterruptsDisabled();
}

void Scheduler::Exit()
{
    asm volatile("cli");

    current->state = TaskState::Zombie;

    Task *next_task = PickNext();
    SwitchTo(next_task);

    // Never returns
    for (;;) asm volatile("hlt");
}