/* Pi 3B / BCM2837 only. Polling drivers; MMU and caches remain disabled.
 * Register layout: Raspberry Pi BCM2835 ARM Peripherals, GPIO and PL011.
 * BCM2837 maps these peripherals at physical 0x3f000000.
 */
#include "../common/hardware.h"
#include <stddef.h>
#include <stdint.h>

#define GPIO_BASE UINT64_C(0x3f200000)
#define UART_BASE UINT64_C(0x3f201000)
#define UART_CLOCK UINT64_C(48000000)

static uint64_t counter_hz;
static int uart_ready;

static void mmio_write(uint64_t address, uint32_t value){
  __asm__ volatile("dsb sy" ::: "memory");
  *(volatile uint32_t*)(uintptr_t)address=value;
  __asm__ volatile("dsb sy" ::: "memory");
}

static uint32_t mmio_read(uint64_t address){
  uint32_t value=*(volatile uint32_t*)(uintptr_t)address;
  __asm__ volatile("dsb sy" ::: "memory");
  return value;
}

static uint64_t counter(void){
  uint64_t ticks;
  __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(ticks) :: "memory");
  return ticks;
}

int64_t tn_hw_micros(void){
  if(!counter_hz) return -1;
  uint64_t ticks=counter();
  return (int64_t)((ticks/counter_hz)*UINT64_C(1000000)+
                   (ticks%counter_hz)*UINT64_C(1000000)/counter_hz);
}

int64_t tn_hw_delay_us(int64_t us){
  if(us<0 || !counter_hz) return -1;
  uint64_t start=(uint64_t)tn_hw_micros();
  while((uint64_t)tn_hw_micros()-start<(uint64_t)us)
    __asm__ volatile("yield" ::: "memory");
  return 0;
}

static void gpio_function(unsigned pin, unsigned function){
  uint64_t address=GPIO_BASE+(pin/10u)*4u;
  unsigned shift=(pin%10u)*3u;
  uint32_t value=mmio_read(address);
  value=(value&~(7u<<shift))|((function&7u)<<shift);
  mmio_write(address,value);
}

int64_t tn_hw_pi3_uart_init(int64_t baud){
  if(baud<=0 || baud>3000000 || !counter_hz) return -1;
  uint64_t divisor=(UART_CLOCK*4u+(uint64_t)baud/2u)/(uint64_t)baud;
  if(divisor<64u || divisor/64u>65535u) return -1;
  mmio_write(UART_BASE+0x30,0);
  gpio_function(14,4); /* ALT0: TXD0 */
  gpio_function(15,4); /* ALT0: RXD0 */
  mmio_write(GPIO_BASE+0x94,0); /* disable pulls on TX/RX */
  tn_hw_delay_us(5);
  mmio_write(GPIO_BASE+0x98,(1u<<14)|(1u<<15));
  tn_hw_delay_us(5);
  mmio_write(GPIO_BASE+0x94,0);
  mmio_write(GPIO_BASE+0x98,0);
  mmio_write(UART_BASE+0x44,0x7ff); /* clear interrupt status */
  mmio_write(UART_BASE+0x38,0);     /* polling, UART IRQs disabled */
  mmio_write(UART_BASE+0x24,(uint32_t)(divisor/64u));
  mmio_write(UART_BASE+0x28,(uint32_t)(divisor%64u));
  mmio_write(UART_BASE+0x2c,(3u<<5)|(1u<<4)); /* 8N1, FIFO */
  mmio_write(UART_BASE+0x30,(1u<<9)|(1u<<8)|1u);
  uart_ready=1;
  return 0;
}

int64_t tn_hw_init(void){
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(counter_hz));
  if(!counter_hz) return -1;
  return tn_hw_pi3_uart_init(115200);
}

int64_t tn_hw_target_id(void){ return 2; }

int64_t tn_hw_uart_write(int64_t pointer_bits, int64_t length){
  if(!uart_ready || length<0 || length>1048576 || (!pointer_bits&&length)) return -1;
  const unsigned char* text=(const unsigned char*)(uintptr_t)pointer_bits;
  uint64_t start=(uint64_t)tn_hw_micros();
  for(int64_t i=0;i<length;i++){
    while(mmio_read(UART_BASE+0x18)&(1u<<5)){
      if((uint64_t)tn_hw_micros()-start>UINT64_C(1000000)) return -1;
    }
    mmio_write(UART_BASE,text[i]);
  }
  return length;
}

int64_t tn_hw_uart_read(int64_t pointer_bits, int64_t capacity){
  if(!uart_ready || capacity<0 || capacity>1048576 || (!pointer_bits&&capacity)) return -1;
  unsigned char* output=(unsigned char*)(uintptr_t)pointer_bits;
  int64_t count=0;
  while(count<capacity && !(mmio_read(UART_BASE+0x18)&(1u<<4))){
    uint32_t value=mmio_read(UART_BASE);
    if(value&0xf00u){ mmio_write(UART_BASE+4,0); return -1; }
    output[count++]=(unsigned char)value;
  }
  return count;
}

int64_t tn_hw_irq_state(void){
  uint64_t flags;
  __asm__ volatile("mrs %0, daif" : "=r"(flags) :: "memory");
  return (flags&(UINT64_C(1)<<7))==0;
}

int64_t tn_hw_irq_enable(void){
  int64_t previous=tn_hw_irq_state();
  __asm__ volatile("msr daifclr, #2; isb" ::: "memory");
  return previous;
}

int64_t tn_hw_irq_disable(void){
  int64_t previous=tn_hw_irq_state();
  __asm__ volatile("msr daifset, #2; isb" ::: "memory");
  return previous;
}

static int valid_pin(int64_t pin){
  return pin>=0 && pin<=27 && !(uart_ready && (pin==14 || pin==15));
}

int64_t tn_hw_pi3_gpio_mode(int64_t pin, int64_t mode){
  if(!valid_pin(pin) || (mode!=0 && mode!=1)) return -1;
  gpio_function((unsigned)pin,(unsigned)mode);
  return 0;
}

int64_t tn_hw_pi3_gpio_write(int64_t pin, int64_t value){
  if(!valid_pin(pin) || (value!=0 && value!=1)) return -1;
  unsigned shift=((unsigned)pin%10u)*3u;
  if(((mmio_read(GPIO_BASE+((unsigned)pin/10u)*4u)>>shift)&7u)!=1u) return -1;
  mmio_write(GPIO_BASE+(value?0x1c:0x28),1u<<(unsigned)pin);
  return 0;
}

int64_t tn_hw_pi3_gpio_read(int64_t pin){
  if(!valid_pin(pin)) return -1;
  return (mmio_read(GPIO_BASE+0x34)>>(unsigned)pin)&1u;
}

static void write_text(const char* text){
  int64_t size=0;
  while(text[size]) size++;
  (void)tn_hw_uart_write((int64_t)(uintptr_t)text,size);
}

static void write_integer(int64_t number){
  char digits[21];
  uint64_t magnitude=number<0 ? 0-(uint64_t)number : (uint64_t)number;
  int count=0;
  do { digits[count++]=(char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
  if(number<0) write_text("-");
  while(count) (void)tn_hw_uart_write((int64_t)(uintptr_t)&digits[--count],1);
}

void tn_hw_finish(int64_t status){
  tn_hw_irq_disable();
  write_text("TEZZ_HW_EXIT=");
  write_integer(status);
  write_text("\n");
  for(;;) __asm__ volatile("wfe" ::: "memory");
}

void tn_hw_exception(int64_t syndrome, int64_t pc){
  write_text("TEZZ_HW_EXCEPTION=");
  write_integer(syndrome);
  write_text(" PC=");
  write_integer(pc);
  write_text("\n");
  tn_hw_finish(-1);
}
