/* Fixed QEMU virt profile: real NS16550 UART and CLINT mtime MMIO. */
#include "../common/hardware.h"
#include <stdint.h>

#define UART_BASE UINT64_C(0x10000000)
#define MTIME_ADDRESS UINT64_C(0x0200bff8)
#define FINISHER_ADDRESS UINT64_C(0x00100000)
enum { MAX_IO=1024*1024 };

static int initialized;
static uint64_t origin;

static inline void mmio_write8(unsigned index, uint8_t value){
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  *(volatile uint8_t*)(uintptr_t)(UART_BASE+index)=value;
  __asm__ volatile("fence iorw,iorw" : : : "memory");
}

static inline uint8_t mmio_read8(unsigned index){
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  uint8_t value=*(volatile uint8_t*)(uintptr_t)(UART_BASE+index);
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  return value;
}

static inline uint64_t mtime(void){
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  uint64_t value=*(volatile uint64_t*)(uintptr_t)MTIME_ADDRESS;
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  return value;
}

int64_t tn_hw_irq_state(void){
  uint64_t state;
  __asm__ volatile("csrr %0,mstatus" : "=r"(state));
  return (state>>3)&1;
}

int64_t tn_hw_irq_enable(void){
  uint64_t previous;
  __asm__ volatile("csrrsi %0,mstatus,8" : "=r"(previous) : : "memory");
  return (previous>>3)&1;
}

int64_t tn_hw_irq_disable(void){
  uint64_t previous;
  __asm__ volatile("csrrci %0,mstatus,8" : "=r"(previous) : : "memory");
  return (previous>>3)&1;
}

int64_t tn_hw_init(void){
  if(initialized) return 0;
  (void)tn_hw_irq_disable();
  /* This profile polls mtime; it does not enable individual IRQ sources. */
  __asm__ volatile("csrw mie,zero" : : : "memory");
  mmio_write8(1,0);
  mmio_write8(3,0x80);
  mmio_write8(0,3);
  mmio_write8(1,0);
  mmio_write8(3,3);
  mmio_write8(2,0xc7);
  mmio_write8(4,0x1e);
  mmio_write8(0,0xae);
  if(mmio_read8(0)!=0xae){ mmio_write8(4,0x0b); return -1; }
  mmio_write8(4,0x0b);
  origin=mtime();
  initialized=1;
  (void)tn_hw_irq_enable();
  return 0;
}

int64_t tn_hw_target_id(void){ return 4; }

int64_t tn_hw_uart_write(int64_t pointer, int64_t length){
  if(!initialized || length<0 || length>MAX_IO || (!pointer && length)) return -1;
  const uint8_t* text=(const uint8_t*)(uintptr_t)(uint64_t)pointer;
  int64_t written=0;
  for(;written<length;written++){
    unsigned tries=1000000;
    while(!(mmio_read8(5)&0x20) && --tries){}
    if(!tries) return written ? written : -1;
    mmio_write8(0,text[written]);
  }
  return written;
}

int64_t tn_hw_uart_read(int64_t pointer, int64_t capacity){
  if(!initialized || capacity<0 || capacity>MAX_IO || (!pointer && capacity)) return -1;
  uint8_t* output=(uint8_t*)(uintptr_t)(uint64_t)pointer;
  int64_t count=0;
  while(count<capacity && (mmio_read8(5)&1)) output[count++]=mmio_read8(0);
  return count;
}

int64_t tn_hw_micros(void){
  if(!initialized) return -1;
  /* This fixed virt profile's CLINT timebase is 10 MHz. */
  return (int64_t)((mtime()-origin)/10);
}

int64_t tn_hw_delay_us(int64_t microseconds){
  if(microseconds<0 || !initialized) return -1;
  uint64_t start=(uint64_t)tn_hw_micros();
  while((uint64_t)tn_hw_micros()-start<(uint64_t)microseconds)
    __asm__ volatile("nop" : : : "memory");
  return 0;
}

static void finish_text(const char* text, size_t length){
  if(initialized) (void)tn_hw_uart_write((int64_t)(uintptr_t)text,(int64_t)length);
}

_Noreturn void tn_hw_finish(int64_t status){
  (void)tn_hw_irq_disable();
  finish_text("TEZZ_HW_EXIT=",13);
  char digits[21];
  size_t count=0;
  uint64_t magnitude=(uint64_t)status;
  if(status<0){ finish_text("-",1); magnitude=0-magnitude; }
  do { digits[count++]=(char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
  while(count) finish_text(&digits[--count],1);
  finish_text("\n",1);
  /* The exact guest status remains in the marker; process status is 0 or 1. */
  uint32_t value=status ? UINT32_C(0x00013333) : UINT32_C(0x00005555);
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  *(volatile uint32_t*)(uintptr_t)FINISHER_ADDRESS=value;
  __asm__ volatile("fence iorw,iorw" : : : "memory");
  for(;;) __asm__ volatile("wfi");
}

_Noreturn void tn_hw_rv_start(void){
  if(tn_hw_init()!=0) tn_hw_finish(-1);
  tn_hw_finish(tn_main());
}
