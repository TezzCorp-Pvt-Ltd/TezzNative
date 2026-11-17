/* Execute AVR machine code with simavr peripherals and independently inspect
 * GPIO, ADC, UART, EEPROM and SRAM/stack state. No native-host HAL is used. */
#include <sim_avr.h>
#include <sim_elf.h>
#include <avr_ioport.h>
#include <avr_uart.h>
#include <avr_adc.h>
#include <avr_eeprom.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  avr_t* avr;
  const char* scenario;
  char uart[1024];
  size_t uart_used;
  int uart_injected;
  int adc_triggers;
  int last_led;
  int led_values[32];
  uint64_t led_cycles[32];
  size_t led_count;
} Observation;

static void uart_output(struct avr_irq_t* irq, uint32_t value, void* parameter){
  (void)irq;
  Observation* observation=(Observation*)parameter;
  if(observation->uart_used+1<sizeof(observation->uart)){
    observation->uart[observation->uart_used++]=(char)(value&255);
    observation->uart[observation->uart_used]=0;
  }
  if(!strcmp(observation->scenario,"uart") && !observation->uart_injected && value=='U'){
    observation->uart_injected=1;
    avr_raise_irq(avr_io_getirq(observation->avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_INPUT),'Z');
  }
}

static void led_change(struct avr_irq_t* irq, uint32_t value, void* parameter){
  (void)irq;
  Observation* observation=(Observation*)parameter;
  int level=value&1;
  if(level!=observation->last_led && observation->led_count<32){
    size_t index=observation->led_count++;
    observation->led_values[index]=level;
    observation->led_cycles[index]=observation->avr->cycle;
    observation->last_led=level;
  }
}

static void adc_trigger(struct avr_irq_t* irq, uint32_t value, void* parameter){
  (void)irq; (void)value;
  Observation* observation=(Observation*)parameter;
  observation->adc_triggers++;
  avr_raise_irq(avr_io_getirq(observation->avr,AVR_IOCTL_ADC_GETIRQ,ADC_IRQ_ADC0),2500);
  avr_raise_irq(avr_io_getirq(observation->avr,AVR_IOCTL_ADC_GETIRQ,ADC_IRQ_ADC1),0);
  avr_raise_irq(avr_io_getirq(observation->avr,AVR_IOCTL_ADC_GETIRQ,ADC_IRQ_ADC2),5000);
}

static uint32_t data_symbol(const elf_firmware_t* firmware, const char* name){
  for(uint32_t i=0;i<firmware->symbolcount;i++){
    if(!strcmp(firmware->symbol[i]->symbol,name)){
      uint32_t address=firmware->symbol[i]->addr;
      return address>=UINT32_C(0x800000) && address<AVR_SEGMENT_OFFSET_EEPROM ? address-UINT32_C(0x800000) : UINT32_MAX;
    }
  }
  return UINT32_MAX;
}

static int fail(const char* reason, const Observation* observation){
  fprintf(stderr,"UNO_SIM_FAIL %s cycle=%" PRIu64 " pc=0x%x uart=%s\n",
          reason,(uint64_t)observation->avr->cycle,observation->avr->pc,observation->uart);
  fprintf(stderr,"USART A=%02x B=%02x C=%02x UBRR=%02x%02x SREG=%02x\n",
          observation->avr->data[0xc0],observation->avr->data[0xc1],observation->avr->data[0xc2],
          observation->avr->data[0xc5],observation->avr->data[0xc4],observation->avr->data[R_SREG]);
  return 1;
}

int main(int argc, char** argv){
  if(argc!=3){ fprintf(stderr,"usage: uno_sim firmware.elf gpio|adc|uart|timing|core\n"); return 2; }
  elf_firmware_t firmware;
  memset(&firmware,0,sizeof(firmware));
  if(elf_read_firmware(argv[1],&firmware)!=0){ fprintf(stderr,"cannot load firmware\n"); return 2; }
  avr_t* avr=avr_make_mcu_by_name("atmega328p");
  if(!avr || avr_init(avr)!=0){ fprintf(stderr,"cannot initialize ATmega328P\n"); return 2; }
  avr->frequency=16000000;
  avr->vcc=avr->avcc=avr->aref=5000;
  avr->log=LOG_NONE;
  avr_load_firmware(avr,&firmware);
  Observation observation;
  memset(&observation,0,sizeof(observation));
  observation.avr=avr;
  observation.scenario=argv[2];
  observation.last_led=-1;
  uint32_t uart_flags=0;
  avr_ioctl(avr,AVR_IOCTL_UART_SET_FLAGS('0'),&uart_flags);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_UART_GETIRQ('0'),UART_IRQ_OUTPUT),uart_output,&observation);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('B'),IOPORT_IRQ_PIN5),led_change,&observation);
  avr_irq_register_notify(avr_io_getirq(avr,AVR_IOCTL_ADC_GETIRQ,ADC_IRQ_OUT_TRIGGER),adc_trigger,&observation);
  avr_ioport_external_t external;
  memset(&external,0,sizeof(external));
  external.name='D'; external.mask=4; external.value=4;
  avr_ioctl(avr,AVR_IOCTL_IOPORT_SET_EXTERNAL('D'),&external);
  avr_raise_irq(avr_io_getirq(avr,AVR_IOCTL_IOPORT_GETIRQ('D'),IOPORT_IRQ_PIN2),1);
  uint32_t finished=data_symbol(&firmware,"tn_hw_finished");
  uint32_t status_address=data_symbol(&firmware,"tn_hw_exit_status");
  uint32_t data_end=data_symbol(&firmware,"__bss_end");
  if(finished>avr->ramend || status_address>(uint32_t)avr->ramend-7 || data_end>avr->ramend)
    return fail("missing/out-of-range runtime status symbols",&observation);
  uint16_t minimum_sp=avr->ramend;
  const uint64_t cycle_limit=UINT64_C(32000000);
  while(avr->cycle<cycle_limit && !avr->data[finished]){
    int state=avr_run(avr);
    uint16_t stack=(uint16_t)(avr->data[R_SPL]|((uint16_t)avr->data[R_SPH]<<8));
    if(stack>=0x100 && stack<minimum_sp) minimum_sp=stack;
    if(state==cpu_Crashed || state==cpu_Done) break;
  }
  if(!avr->data[finished]) return fail("firmware crashed or exceeded cycle budget",&observation);
  uint64_t status_bits=0;
  for(int i=0;i<8;i++) status_bits|=(uint64_t)avr->data[status_address+i]<<(8*i);
  int64_t status;
  memcpy(&status,&status_bits,sizeof(status));
  if(status!=0){
    fprintf(stderr,"UNO status=%" PRId64 "\n",status);
    return fail("Tezz fixture returned failure",&observation);
  }
  if(!strstr(observation.uart,"TEZZ_HW_EXIT=0\n")) return fail("missing UART completion trailer",&observation);
  if(minimum_sp<data_end+32) return fail("stack collided with static SRAM/reserve",&observation);
  if(!strcmp(argv[2],"gpio")){
    avr_ioport_state_t b,d;
    memset(&b,0,sizeof(b)); memset(&d,0,sizeof(d));
    avr_ioctl(avr,AVR_IOCTL_IOPORT_GETSTATE('B'),&b);
    avr_ioctl(avr,AVR_IOCTL_IOPORT_GETSTATE('D'),&d);
    if((b.ddr&33)!=33 || (b.port&33)!=33 || (d.ddr&128)!=0 || (d.port&128)!=128 || !(d.pin&4))
      return fail("GPIO DDR/latch/pullup/input state mismatch",&observation);
    int sequence=0;
    uint64_t previous=0;
    for(size_t i=0;i<observation.led_count;i++){
      int expected=sequence==1 ? 0 : 1;
      if(observation.led_values[i]==expected){
        if(sequence && observation.led_cycles[i]-previous<1600)
          return fail("GPIO pulse shorter than requested 100us delay",&observation);
        previous=observation.led_cycles[i];
        if(++sequence==3) break;
      }
    }
    if(sequence!=3) return fail("missing PB5 high/low/high waveform",&observation);
  } else if(!strcmp(argv[2],"adc")){
    if(observation.adc_triggers!=3) return fail("ADC conversion count mismatch",&observation);
    avr_eeprom_desc_t descriptor;
    memset(&descriptor,0,sizeof(descriptor));
    descriptor.offset=0; descriptor.size=1024;
    avr_ioctl(avr,AVR_IOCTL_EEPROM_GET,&descriptor);
    if(!descriptor.ee || descriptor.ee[0]!=42 || descriptor.ee[1023]!=165)
      return fail("EEPROM peripheral did not retain firmware writes",&observation);
  } else if(!strcmp(argv[2],"uart")){
    if(!observation.uart_injected || !strstr(observation.uart,"UNO\n")) return fail("UART TX/RX sequence mismatch",&observation);
    /* ATmega328P UBRR0=207, U2X0=1 gives 9615 baud at 16 MHz. */
    if(avr->data[0xc4]!=207 || avr->data[0xc5]!=0 || !(avr->data[0xc0]&2))
      return fail("UART 9600 divisor/format mismatch",&observation);
  } else if(strcmp(argv[2],"timing") && strcmp(argv[2],"core")) return fail("unknown scenario",&observation);
  printf("UNO_SIM_PASS scenario=%s cycles=%" PRIu64 " min_sp=0x%x static_end=0x%x physical_hardware=false\n",
         argv[2],(uint64_t)avr->cycle,minimum_sp,data_end);
  avr_terminate(avr);
  return 0;
}
