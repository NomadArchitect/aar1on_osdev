//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <printf.h>
#include <panic.h>

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <mm.h>

#include <acpi.h>
#include <percpu.h>
#include <smpboot.h>
#include <syscall.h>
#include <timer.h>
#include <scheduler.h>

#include <drivers/serial.h>
#include <drivers/ahci.h>

#include <device/apic.h>
#include <device/ioapic.h>
#include <device/pic.h>

#include <loader.h>
#include <fs.h>
#include <fs/utils.h>
#include <fs/path.h>
#include <fs/blkdev.h>
// #include <fat/fat.h>

#include <bus/pcie.h>
#include <usb/usb.h>
#include <usb/scsi.h>
#include <event.h>
#include <gui/screen.h>
#include <signal.h>
#include <ipc.h>

boot_info_t *boot_info;
percpu_t *cpu;

extern uint16_t mouse_x;
extern uint16_t mouse_y;

#define W 0xFFFFFFFF
#define B 0x000000FF
const uint32_t cursor_bmp[19][12] = {
  { B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { B, B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { B, W, B, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { B, W, W, B, 0, 0, 0, 0, 0, 0, 0, 0 },
  { B, W, W, W, B, 0, 0, 0, 0, 0, 0, 0 },
  { B, W, W, W, W, B, 0, 0, 0, 0, 0, 0 },
  { B, W, W, W, W, W, B, 0, 0, 0, 0, 0 },
  { B, W, W, W, W, W, W, B, 0, 0, 0, 0 },
  { B, W, W, W, W, W, W, W, B, 0, 0, 0 },
  { B, W, W, W, W, W, W, W, W, B, 0, 0 },
  { B, W, W, W, W, W, W, W, W, W, B, 0 },
  { B, W, W, W, W, W, W, W, W, W, W, B },
  { B, W, W, W, W, W, W, B, B, B, B, B },
  { B, W, W, W, B, W, W, B, 0, 0, 0, 0 },
  { B, W, W, B, 0, B, W, W, B, 0, 0, 0 },
  { B, W, B, 0, 0, B, W, W, B, 0, 0, 0 },
  { B, B, 0, 0, 0, 0, B, W, W, B, 0, 0 },
  { 0, 0, 0, 0, 0, 0, B, W, W, B, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, B, B, 0, 0, 0 },
};
#undef W
#undef B

const char *argv[] = {
  "/usr/bin/hello",
  NULL
};

noreturn void example_process() {
  kprintf("receiving message\n");
  message_t *msg = ipc_receive();
  kprintf("msg->origin: %d\n", msg->origin);
  kprintf("msg->type: %d\n", msg->type);
  kfree(msg);
  thread_block();
  while (true) {};
}

noreturn void wakeup_process() {
  thread_sleep(5e6);
  kprintf("unblocked\n");
  thread_block();
  while (true) {}
}

//
// Kernel launch process
//

_Noreturn void launch() {
  sti();
  timer_init();

  kprintf("[pid %d] launch\n", ID);

  fs_init();

  pcie_init();
  pcie_discover();
  ahci_init();

  usb_init();
  events_init();

  // if (fs_mount("/", "/dev/sdb", "ext2") < 0) {
  //   kprintf("%s\n", strerror(ERRNO));
  // }

  // pid_t target = process_create(example_process);
  // message_t msg = {
  //   .origin = 1,
  //   .type = 2,
  // };
  // ipc_send(target, &msg);

  // fs_open("/dev/stdin", O_RDONLY, 0);
  // fs_open("/dev/stdout", O_WRONLY, 0);
  // fs_open("/dev/stderr", O_WRONLY, 0);
  // process_execve("/bin/winserv", (void *) argv, NULL);
  process_create(wakeup_process);
  uint32_t color = (129 << 16) | (129 << 8) | (1 << 0);

  uint32_t width = boot_info->fb_width;
  uint32_t height = boot_info->fb_height;
  size_t len = width * height;
  uint32_t *fb = (void *) FRAMEBUFFER_VA;
  while (true) {
    kprintf("[main] mouse_x: %d\n", mouse_x);
    kprintf("[main] mouse_y: %d\n", mouse_y);
    kassert(mouse_x <= width);
    kassert(mouse_y <= height);
    for (int i = 0; i < len; i++) {
      fb[i] = color;
    }

    int max_y = min(19, height - mouse_y);
    int max_x = min(12, width - mouse_y);
    for (int y = 0; y < max_y; y++) {
      for (int x = 0; x < max_x; x++) {
        uint32_t value = cursor_bmp[y][x];
        if (value == 0) {
          continue;
        }

        int index = (mouse_y + y) * width + (x + mouse_x);
        fb[index] = value;
      }
    }
    thread_sleep(33333);
  }

  kprintf("done!\n");
  thread_block();
}

//
// Kernel entry
//

__used void kmain(boot_info_t *info) {
  boot_info = info;
  percpu_init();
  enable_sse();

  cpu = PERCPU;
  serial_init(COM1);
  kprintf("[kernel] initializing\n");

  setup_gdt();
  setup_idt();

  kheap_init();

  mm_init();
  vm_init();

  acpi_init();

  pic_init();
  apic_init();
  ioapic_init();

  syscalls_init();
  // smp_init();

  // root process
  process_t *root = create_root_process(launch);
  scheduler_init(root);
}

__used void ap_main() {
  percpu_init();
  enable_sse();

  kprintf("[CPU#%d] initializing\n", ID);

  setup_gdt();
  setup_idt();

  vm_init();
  apic_init();
  ioapic_init();

  kprintf("[CPU#%d] done!\n", ID);
}
