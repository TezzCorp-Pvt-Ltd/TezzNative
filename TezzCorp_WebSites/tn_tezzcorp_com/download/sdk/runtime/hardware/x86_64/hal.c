/* QEMU PC BIOS profile: legacy PIC/PIT and a 16550 UART at COM1. */
#include "../common/hardware.h"
#include <stddef.h>
#include <stdint.h>

enum { COM1=0x3f8, PIT_DIVISOR=1193, PIT_HZ=1193182, MAX_IO=1024*1024 };

typedef struct __attribute__((packed)) {
  uint16_t low, selector;
  uint8_t ist, attributes;
  uint16_t middle;
  uint32_t high, reserved;
} IdtGate;

typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint64_t address;
} IdtPointer;

_Static_assert(sizeof(IdtGate)==16, "x86_64 IDT gate layout");
_Static_assert(sizeof(IdtPointer)==10, "x86_64 IDTR layout");

static IdtGate idt[256] __attribute__((aligned(16)));
volatile uint64_t tn_hw_x86_ticks __attribute__((aligned(8)));
static uint64_t last_microseconds;
static int initialized;
extern void tn_hw_x86_timer_entry(void);
extern void tn_hw_x86_fault_entry(void);
extern int64_t tn_main(void);

static inline void out8(uint16_t port, uint8_t value){
  __asm__ volatile("outb %0,%1" : : "a"(value), "Nd"(port));
}

static inline uint8_t in8(uint16_t port){
  uint8_t value;
  __asm__ volatile("inb %1,%0" : "=a"(value) : "Nd"(port));
  return value;
}

static inline uint64_t irq_save(void){
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
  return flags;
}

static inline void irq_restore(uint64_t flags){
  if(flags & 0x200) __asm__ volatile("sti" : : : "memory");
}

static void io_wait(void){ out8(0x80,0); }

static void idt_set(unsigned index, void (*entry)(void)){
  uintptr_t address=(uintptr_t)entry;
  idt[index]=(IdtGate){(uint16_t)address,8,0,0x8e,
                      (uint16_t)(address>>16),(uint32_t)(address>>32),0};
}

static int serial_init(void){
  out8(COM1+1,0);
  out8(COM1+3,0x80);
  out8(COM1,1);                  /* 115200 baud */
  out8(COM1+1,0);
  out8(COM1+3,3);                /* 8 data bits, no parity, one stop bit */
  out8(COM1+2,0xc7);
  out8(COM1+4,0x1e);             /* UART-local loopback self-test */
  out8(COM1,0xae);
  if(in8(COM1)!=0xae){ out8(COM1+4,0x0b); return -1; }
  out8(COM1+4,0x0b);
  return 0;
}

int64_t tn_hw_init(void){
  uint64_t flags=irq_save();
  if(initialized){ irq_restore(flags); return 0; }
  if(serial_init()!=0){ irq_restore(flags); return -1; }
  for(unsigned i=0;i<256;i++) idt_set(i,tn_hw_x86_fault_entry);
  idt_set(32,tn_hw_x86_timer_entry);
  IdtPointer pointer={(uint16_t)(sizeof(idt)-1),(uint64_t)(uintptr_t)idt};
  __asm__ volatile("lidt %0" : : "m"(pointer) : "memory");

  /* Remap the two legacy PICs; only the PIT IRQ0 is unmasked. */
  out8(0x20,0x11); io_wait();
  out8(0xa0,0x11); io_wait();
  out8(0x21,0x20); io_wait();
  out8(0xa1,0x28); io_wait();
  out8(0x21,4); io_wait();
  out8(0xa1,2); io_wait();
  out8(0x21,1); io_wait();
  out8(0xa1,1); io_wait();
  out8(0x21,0xfe);
  out8(0xa1,0xff);
  tn_hw_x86_ticks=0;
  last_microseconds=0;
  out8(0x43,0x34);               /* channel0, low/high bytes, rate generator */
  out8(0x40,PIT_DIVISOR & 0xff);
  out8(0x40,PIT_DIVISOR >> 8);
  initialized=1;
  __asm__ volatile("sti" : : : "memory");
  return 0;
}

int64_t tn_hw_target_id(void){ return 3; }

int64_t tn_hw_uart_write(int64_t pointer, int64_t length){
  if(!initialized || length<0 || length>MAX_IO || (!pointer && length)) return -1;
  const uint8_t* text=(const uint8_t*)(uintptr_t)(uint64_t)pointer;
  int64_t written=0;
  for(;written<length;written++){
    unsigned tries=1000000;
    while(!(in8(COM1+5)&0x20) && --tries){}
    if(!tries) return written ? written : -1;
    out8(COM1,text[written]);
  }
  return written;
}

int64_t tn_hw_uart_read(int64_t pointer, int64_t capacity){
  if(!initialized || capacity<0 || capacity>MAX_IO || (!pointer && capacity)) return -1;
  uint8_t* output=(uint8_t*)(uintptr_t)(uint64_t)pointer;
  int64_t count=0;
  while(count<capacity && (in8(COM1+5)&1)) output[count++]=in8(COM1);
  return count;
}

int64_t tn_hw_irq_state(void){
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0" : "=r"(flags));
  return (flags>>9)&1;
}

int64_t tn_hw_irq_enable(void){
  int64_t previous=tn_hw_irq_state();
  if(initialized) __asm__ volatile("sti" : : : "memory");
  return previous;
}

int64_t tn_hw_irq_disable(void){ return (irq_save()>>9)&1; }

static uint16_t pit_counter(void){
  out8(0x43,0);                  /* latch current channel0 count */
  uint16_t low=in8(0x40);
  return low | ((uint16_t)in8(0x40)<<8);
}

static int timer_pending(void){
  out8(0x20,0x0a);               /* read PIC interrupt-request register */
  return in8(0x20)&1;
}

int64_t tn_hw_micros(void){
  if(!initialized) return -1;
  uint64_t flags=irq_save();
  uint64_t ticks=tn_hw_x86_ticks;
  int pending_before=timer_pending();
  uint16_t counter=pit_counter();
  int pending_after=timer_pending();
  /* A latched IRQ has not yet reached the assembly tick counter. If it arrived
   * during sampling, re-latch the PIT so count and IRQ refer to the same cycle. */
  if(pending_before || pending_after){
    ticks++;
    if(!pending_before) counter=pit_counter();
  }
  if(!counter || counter>PIT_DIVISOR) counter=PIT_DIVISOR;
  uint64_t partial=(ticks%PIT_HZ)*PIT_DIVISOR+(PIT_DIVISOR-counter);
  uint64_t microseconds=(ticks/PIT_HZ)*(UINT64_C(1000000)*PIT_DIVISOR)
                       +partial*UINT64_C(1000000)/PIT_HZ;
  if(microseconds<last_microseconds) microseconds=last_microseconds;
  else last_microseconds=microseconds;
  irq_restore(flags);
  return (int64_t)microseconds;
}

int64_t tn_hw_delay_us(int64_t microseconds){
  if(microseconds<0 || !initialized) return -1;
  if(!microseconds) return 0;
  if(!tn_hw_irq_state()) return -1;
  uint64_t start=(uint64_t)tn_hw_micros();
  while((uint64_t)tn_hw_micros()-start<(uint64_t)microseconds)
    __asm__ volatile("hlt" : : : "memory");
  return 0;
}

static void finish_text(const char* text, size_t length){
  for(size_t i=0;i<length;i++) out8(0xe9,(uint8_t)text[i]);
  if(initialized) (void)tn_hw_uart_write((int64_t)(uintptr_t)text,(int64_t)length);
}

__attribute__((noreturn)) void tn_hw_finish(int64_t status){
  __asm__ volatile("cli" : : : "memory");
  finish_text("TEZZ_HW_EXIT=",13);
  char digits[21];
  size_t count=0;
  uint64_t magnitude=(uint64_t)status;
  if(status<0){ finish_text("-",1); magnitude=0-magnitude; }
  do { digits[count++]=(char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
  while(count) finish_text(&digits[--count],1);
  finish_text("\n",1);
  uint32_t result=status ? 0x11 : 0x10;
  __asm__ volatile("outl %0,%1" : : "a"(result), "Nd"((uint16_t)0xf4));
  for(;;) __asm__ volatile("hlt");
}

__attribute__((noreturn)) void tn_hw_x86_start(void){
  if(tn_hw_init()!=0) tn_hw_finish(-1);
  tn_hw_finish(tn_main());
}
