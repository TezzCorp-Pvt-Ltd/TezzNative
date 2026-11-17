/* Arduino UNO R3 / ATmega328P, 16 MHz. This code runs on the AVR core, never
 * on the compiler host. Public entrypoints use the Tezz IR's int64 value ABI. */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include <util/delay_basic.h>
#include <stdint.h>

#if !defined(__AVR_ATmega328P__) || F_CPU != 16000000UL
#error "arduino_uno.c requires ATmega328P at 16 MHz"
#endif

static volatile uint64_t timer_ticks;
static uint8_t adc_reference=1;
static uint8_t uart_pending;
volatile int64_t tn_hw_exit_status;
volatile uint8_t tn_hw_finished;
int64_t tn_uno_uart_flush(void);

ISR(TIMER0_OVF_vect){ timer_ticks+=256; }

static int pin_registers(int64_t pin, volatile uint8_t** direction,
                         volatile uint8_t** output, volatile uint8_t** input,
                         uint8_t* mask){
  if(pin<0 || pin>19) return 0;
  if(pin<8){
    *direction=&DDRD; *output=&PORTD; *input=&PIND; *mask=(uint8_t)(1u<<(uint8_t)pin);
  } else if(pin<14){
    *direction=&DDRB; *output=&PORTB; *input=&PINB; *mask=(uint8_t)(1u<<(uint8_t)(pin-8));
  } else {
    *direction=&DDRC; *output=&PORTC; *input=&PINC; *mask=(uint8_t)(1u<<(uint8_t)(pin-14));
  }
  return 1;
}

int64_t tn_uno_pin_mode(int64_t pin, int64_t mode){
  volatile uint8_t *direction,*output,*input;
  uint8_t mask;
  if(mode<0 || mode>2 || !pin_registers(pin,&direction,&output,&input,&mask)) return -1;
  uint8_t saved=SREG;
  cli();
  if(mode==1) *direction|=mask;
  else {
    *direction&=(uint8_t)~mask;
    if(mode==2) *output|=mask;
    else *output&=(uint8_t)~mask;
  }
  SREG=saved;
  return 0;
}

int64_t tn_uno_digital_write(int64_t pin, int64_t value){
  volatile uint8_t *direction,*output,*input;
  uint8_t mask;
  if((value!=0 && value!=1) || !pin_registers(pin,&direction,&output,&input,&mask)) return -1;
  uint8_t saved=SREG;
  cli();
  if(value) *output|=mask;
  else *output&=(uint8_t)~mask;
  SREG=saved;
  return 0;
}

int64_t tn_uno_digital_read(int64_t pin){
  volatile uint8_t *direction,*output,*input;
  uint8_t mask;
  if(!pin_registers(pin,&direction,&output,&input,&mask)) return -1;
  return (*input&mask)!=0;
}

int64_t tn_uno_digital_toggle(int64_t pin){
  volatile uint8_t *direction,*output,*input;
  uint8_t mask;
  if(!pin_registers(pin,&direction,&output,&input,&mask)) return -1;
  /* Writing a one to PIN toggles the corresponding PORT latch atomically. */
  *input=mask;
  return 0;
}

static volatile uint8_t* port_register(int64_t port, int input){
  if(port==0) return input ? &PINB : &PORTB;
  if(port==1) return input ? &PINC : &PORTC;
  if(port==2) return input ? &PIND : &PORTD;
  return 0;
}

int64_t tn_uno_port_write(int64_t port, int64_t mask, int64_t value){
  volatile uint8_t* output=port_register(port,0);
  int64_t valid=port==2 ? 255 : 63;
  if(!output || mask<0 || (mask&~valid)!=0 || value<0 || value>255) return -1;
  uint8_t saved=SREG;
  cli();
  *output=(uint8_t)((*output&~(uint8_t)mask)|((uint8_t)value&(uint8_t)mask));
  SREG=saved;
  return 0;
}

int64_t tn_uno_port_read(int64_t port){
  volatile uint8_t* input=port_register(port,1);
  if(!input) return -1;
  return *input&(port==2 ? 255 : 63);
}

int64_t tn_hw_irq_enable(void){
  uint8_t saved=SREG;
  sei();
  return (saved&_BV(SREG_I))!=0;
}

int64_t tn_hw_irq_disable(void){
  uint8_t saved=SREG;
  cli();
  return (saved&_BV(SREG_I))!=0;
}

int64_t tn_hw_irq_state(void){ return (SREG&_BV(SREG_I))!=0; }

int64_t tn_hw_micros(void){
  uint8_t saved=SREG;
  cli();
  uint64_t ticks=timer_ticks;
  uint8_t low=TCNT0;
  if((TIFR0&_BV(TOV0)) && low<255) ticks+=256;
  SREG=saved;
  return (int64_t)((ticks+low)*4);
}

int64_t tn_uno_millis(void){ return tn_hw_micros()/1000; }

int64_t tn_hw_delay_us(int64_t microseconds){
  if(microseconds<0) return -1;
  /* Four CPU cycles per loop, four loops per microsecond at 16 MHz. Loop
   * chunks avoid the 16-bit counter overflow; interrupts may extend the wait. */
  while(microseconds){
    uint16_t part=microseconds>16000 ? 16000 : (uint16_t)microseconds;
    _delay_loop_2((uint16_t)(part*4));
    microseconds-=part;
  }
  return 0;
}

int64_t tn_uno_delay_ms(int64_t milliseconds){
  if(milliseconds<0) return -1;
  while(milliseconds--){ tn_hw_delay_us(1000); }
  return 0;
}

int64_t tn_uno_analog_reference(int64_t reference){
  if(reference!=0 && reference!=1 && reference!=3) return -1;
  adc_reference=(uint8_t)reference;
  return 0;
}

int64_t tn_uno_analog_read(int64_t pin){
  if(pin>=14 && pin<=19) pin-=14;
  if(pin<0 || pin>5) return -1;
  ADCSRA=_BV(ADEN)|_BV(ADPS2)|_BV(ADPS1)|_BV(ADPS0);
  ADMUX=(uint8_t)((adc_reference<<6)|(uint8_t)pin);
  ADCSRA|=_BV(ADSC);
  while(ADCSRA&_BV(ADSC)){}
  return ADC;
}

int64_t tn_uno_eeprom_read(int64_t address){
  if(address<0 || address>1023) return -1;
  return eeprom_read_byte((const uint8_t*)(uintptr_t)address);
}

int64_t tn_uno_eeprom_write(int64_t address, int64_t value){
  if(address<0 || address>1023 || value<0 || value>255) return -1;
  eeprom_update_byte((uint8_t*)(uintptr_t)address,(uint8_t)value);
  eeprom_busy_wait();
  return 0;
}

int64_t tn_uno_uart_begin(int64_t baud){
  if(baud<300 || baud>1000000) return -1;
  uint32_t rate=(uint32_t)baud;
  uint32_t divisor=(F_CPU+rate*4)/(rate*8);
  uint8_t fast=1;
  if(divisor>4096){ fast=0; divisor=(F_CPU+rate*8)/(rate*16); }
  if(!divisor || divisor>4096) return -1;
  if(uart_pending && tn_uno_uart_flush()<0) return -1;
  UCSR0A=fast ? _BV(U2X0) : 0;
  UBRR0H=(uint8_t)((divisor-1)>>8);
  UBRR0L=(uint8_t)(divisor-1);
  UCSR0C=_BV(UCSZ01)|_BV(UCSZ00);
  UCSR0B=_BV(RXEN0)|_BV(TXEN0);
  uart_pending=0;
  return 0;
}

int64_t tn_uno_uart_write_byte(int64_t value){
  if(value<0 || value>255 || !(UCSR0B&_BV(TXEN0))) return -1;
  while(!(UCSR0A&_BV(UDRE0))){}
  UCSR0A|=_BV(TXC0);
  UDR0=(uint8_t)value;
  uart_pending=1;
  return 1;
}

int64_t tn_uno_uart_available(void){ return (UCSR0A&_BV(RXC0))!=0; }

int64_t tn_uno_uart_read_byte(void){
  if(!(UCSR0B&_BV(RXEN0)) || !(UCSR0A&_BV(RXC0))) return -1;
  uint8_t status=UCSR0A;
  uint8_t value=UDR0;
  if(status&(_BV(FE0)|_BV(DOR0)|_BV(UPE0))) return -1;
  return value;
}

int64_t tn_uno_uart_flush(void){
  if(!(UCSR0B&_BV(TXEN0))) return -1;
  if(uart_pending){ while(!(UCSR0A&_BV(TXC0))){} }
  uart_pending=0;
  return 0;
}

static int valid_sram(int64_t pointer, int64_t length){
  return length>=0 && (length==0 || (pointer>=0x100 && pointer<=RAMEND && length<=RAMEND-pointer+1));
}

int64_t tn_hw_uart_write(int64_t pointer, int64_t length){
  if(!valid_sram(pointer,length)) return -1;
  const uint8_t* data=(const uint8_t*)(uintptr_t)pointer;
  for(int64_t i=0;i<length;i++) if(tn_uno_uart_write_byte(data[i])<0) return -1;
  return length;
}

int64_t tn_hw_uart_read(int64_t pointer, int64_t capacity){
  if(!valid_sram(pointer,capacity)) return -1;
  uint8_t* data=(uint8_t*)(uintptr_t)pointer;
  int64_t count=0;
  while(count<capacity && tn_uno_uart_available()){
    int64_t value=tn_uno_uart_read_byte();
    if(value<0) return -1;
    data[count++]=(uint8_t)value;
  }
  return count;
}

int64_t tn_hw_target_id(void){ return 1; }

int64_t tn_hw_init(void){
  cli();
  timer_ticks=0;
  TCNT0=0;
  TCCR0A=_BV(WGM01)|_BV(WGM00);
  TCCR0B=_BV(CS01)|_BV(CS00);
  TIFR0=_BV(TOV0);
  TIMSK0|=_BV(TOIE0);
  adc_reference=1;
  tn_hw_finished=0;
  tn_hw_exit_status=0;
  tn_uno_uart_begin(115200);
  sei();
  return 0;
}

void tn_hw_finish(int64_t status){
  static const char prefix[]="TEZZ_HW_EXIT=";
  if(!(UCSR0B&_BV(TXEN0))) tn_uno_uart_begin(115200);
  for(uint8_t i=0;i<sizeof(prefix)-1;i++) tn_uno_uart_write_byte(prefix[i]);
  uint64_t magnitude=(uint64_t)status;
  if(status<0){ tn_uno_uart_write_byte('-'); magnitude=0-magnitude; }
  char digits[20];
  uint8_t used=0;
  do { digits[used++]=(char)('0'+magnitude%10); magnitude/=10; } while(magnitude);
  while(used) tn_uno_uart_write_byte(digits[--used]);
  tn_uno_uart_write_byte('\n');
  tn_uno_uart_flush();
  tn_hw_exit_status=status;
  tn_hw_finished=1;
  cli();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  for(;;){ sleep_cpu(); }
}

extern int64_t tn_main(void);
int main(void){
  tn_hw_init();
  tn_hw_finish(tn_main());
  return 0;
}
