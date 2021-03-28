//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <process.h>
#include <scheduler.h>
#include <string.h>
#include <atomic.h>
#include <thread.h>

#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <fs.h>
#include <panic.h>

#include <printf.h>

static uint64_t __pid = 0;

uint64_t alloc_pid() {
  return atomic_fetch_add(&__pid, 1);
}

noreturn void *root_process_wrapper(void *root_fn) {
  void (*fn)() = root_fn;
  fn();
  while (true) {
    cpu_pause();
  }
}

//

process_t *process_alloc(pid_t pid, pid_t ppid, void *(start_routine)(void *), void *arg) {
  process_t *process = kmalloc(sizeof(process_t));
  memset(process, 0, sizeof(process_t));

  process->pid = pid;
  process->ppid = ppid;
  process->vm = VM;

  process->uid = -1;
  process->gid = -1;
  process->pwd = fs_root;
  process->files = create_file_table();

  thread_t *main = thread_alloc(0, start_routine, arg);
  main->process = process;
  process->main = main;
  process->threads = main;

  process->next = NULL;
  process->prev = NULL;
  return process;
}

void process_free(process_t *process) {
  thread_t *thread = process->main;
  while (thread) {
    thread_t *next = thread->next;
    thread_free(thread);
    thread = next;
  }

  // free file table
  // free vm
  kfree(process);
}

//

process_t *create_root_process(void (function)()) {
  pid_t pid = alloc_pid();
  process_t *process = process_alloc(pid, -1, root_process_wrapper, function);
  return process;
}

//

pid_t fork() {
  kprintf("[process] creating process\n");
  process_t *parent = current_process;
  thread_t *parent_thread = current_thread;

  process_t *process = kmalloc(sizeof(process_t));
  process->pid = alloc_pid();
  process->ppid = parent->pid;
  process->pwd = parent->pwd;
  process->files = copy_file_table(parent->files);
  process->vm = vm_duplicate();

  // clone main thread
  thread_t *main = thread_copy(parent_thread);

  uintptr_t stack = main->stack->addr;
  uintptr_t frame = (uintptr_t) __builtin_frame_address(0);
  uintptr_t offset = frame - STACK_VA;
  uintptr_t rsp = stack + STACK_SIZE - offset;
  main->ctx->rsp = rsp;
  kprintf("[process] rsp: %p\n", rsp);

  process->main = main;
  process->threads = main;

  scheduler_add(main);
  return process->pid;
}

pid_t getpid() {
  return current_process->pid;
}

pid_t getppid() {
  return current_process->ppid;
}

id_t gettid() {
  return current_thread->tid;
}

//

void print_debug_process(process_t *process) {
  kprintf("process:\n");
  kprintf("  pid: %d\n", process->pid);
  kprintf("  ppid: %d\n", process->ppid);

  thread_t *thread = process->main;
  while (thread) {
    print_debug_thread(thread);
    thread = thread->g_next;
  }
}
