/* SPDX-License-Identifier: BSD-3-Clause */
#include "retro8.h"
#include "msx1_test.h"
#include "pico.h"
#include "Z80.h"
#include "tms9918.h"
void msx1_render_line_index(tms9918_context_t *,uint32_t *,int);
#include "mapper.h"
#include "emu2149.h"
#include "emu2212.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

enum { CLOCK=3579545, LINE_CYCLES=228, LINES=262, RATE=44100, PICTURE_BYTES=128*192 };
static Z80 cpu;
static tms9918_context_t vdp;
static mapper_t cart;
static PSG *psg;
static SCC *scc;
static uint8_t ram[65536];
static const uint8_t *system_rom;
static uint8_t keys[16],joy[2],primary,ppi_c,ppi_mode,psg_index;
static uint32_t plain_base;
static uint8_t *picture,*completed_picture,*picture_base;
static const uint16_t display555[16]={0,0,0x2324,0x3f6b,0x754a,0x7dcf,0x255a,0x7ba8,0x295f,0x3dff,0x2b1a,0x433c,0x1ec4,0x5d79,0x6739,0x7fff};
static uint32_t palette_pairs[256];
static uint32_t line_pixels[256];
static r8_audio_fn emit;
static r8_service_fn service_cb;
static bool loaded;
static const char *failure="";
static uint64_t cpu_time,beam_time,audio_time,run_base;
static uint32_t sample_phase;
static int32_t hp_x,hp_y;
static uint64_t psg_writes,joy_reads,key_reads,pcm_samples;
static unsigned scc_bank;
static const uint8_t *read_pages[4];
static uint8_t *write_pages[4];
static uint64_t halt_ticks;
static uint32_t halt_spans;

static void map_pages(void){
    for(unsigned page=0;page<4;page++){
        unsigned slot=(primary>>(page*2))&3;
        unsigned base=page*16384;
        read_pages[page]=NULL;write_pages[page]=NULL;
        if(slot==3){read_pages[page]=ram+base;write_pages[page]=ram+base;}
        else if(slot==0&&page<2)read_pages[page]=system_rom+base;
        else if(slot==1&&cart.type==MAPPER_PLAIN&&base>=plain_base){
            unsigned off=base-plain_base;
            if(off<=cart.size&&cart.size-off>=16384)read_pages[page]=cart.rom+off;
        }
    }
}
void r8_msx_halt_get(uint64_t *ticks,uint32_t *spans){*ticks=halt_ticks;*spans=halt_spans;}

static void __not_in_flash_func(audio_to)(uint64_t time) {
    if(time<=audio_time)return;
    uint64_t phase=(uint64_t)sample_phase+(time-audio_time)*RATE;
    audio_time=time;
    while(phase>=CLOCK){
        phase-=CLOCK;
        int32_t mixed=PSG_calc(psg)*2;
        if(cart.type==MAPPER_KONAMI_SCC)mixed+=SCC_calc(scc);

        int32_t y=mixed-hp_x+hp_y-(hp_y/4096);
        hp_x=mixed;hp_y=y;
        if(y>32767)y=32767;
        if(y< -32768)y=-32768;
        if(emit)emit((int16_t)y,(int16_t)y);
        pcm_samples++;
    }
    sample_phase=(uint32_t)phase;
}
static void irq_update(void){z80_int(&cpu,(vdp.vdpStatus&0x80)&&(vdp.registers[1]&0x20));}
static void irq_assert(void){z80_int(&cpu,1);}
static uint8_t __not_in_flash_func(memory_read)(void *ctx,uint16_t a){
    (void)ctx;
    const uint8_t *page=read_pages[a>>14];
    if(page)return page[a&16383];
    unsigned slot=(primary>>((a>>14)*2))&3;
    if(slot==3)return ram[a];
    if(slot==0)return a<32768?system_rom[a]:0xff;
    if(slot!=1)return 0xff;
    if(cart.type==MAPPER_PLAIN){
        uint32_t offset=(uint32_t)a-plain_base;
        return offset<cart.size?cart.rom[offset]:0xff;
    }
    if(a<0x4000||a>=0xc000)return 0xff;
    if(cart.type==MAPPER_KONAMI_SCC&&scc_bank==0x3f&&(a&0xff00)==0x9800)
        return (uint8_t)SCC_read(scc,a);
    return mapper_read(&cart,a);
}
static void __not_in_flash_func(memory_write)(void *ctx,uint16_t a,uint8_t v){
    (void)ctx;
    uint8_t *page=write_pages[a>>14];
    if(page){page[a&16383]=v;return;}
    unsigned slot=(primary>>((a>>14)*2))&3;
    if(slot!=1||a<0x4000||a>=0xc000)return;
    if(cart.type==MAPPER_KONAMI_SCC){
        if((a&0xf800)==0x9000){
            audio_to(run_base+cpu.cycles);scc_bank=v&0x3f;SCC_write(scc,0x9000,scc_bank);
        }else if(scc_bank==0x3f&&(a&0xff00)==0x9800){
            audio_to(run_base+cpu.cycles);SCC_write(scc,a,v);
        }
    }
    mapper_write(&cart,a,v);
}
static uint8_t __not_in_flash_func(fetch_opcode)(void *ctx,uint16_t a){
    z80_wait(&cpu,1);
    return memory_read(ctx,a);
}

static uint8_t __not_in_flash_func(halt_fetch)(void *ctx,uint16_t a){
    size_t left=cpu.cycle_limit>cpu.cycles?cpu.cycle_limit-cpu.cycles:0;
    size_t n=(!cpu.request&&left)?(left+4)/5:1;
    cpu.r+=(uint8_t)(n-1);
    z80_wait(&cpu,n*5-4);
    halt_ticks+=n*5;halt_spans++;
    return memory_read(ctx,a);
}
static uint8_t __not_in_flash_func(port_read)(void *ctx,uint16_t port){
    (void)ctx;
    switch(port&255){
    case 0x98:return tms9918_read(&vdp,false);
    case 0x99:{uint8_t r=tms9918_read(&vdp,true);irq_update();return r;}
    case 0xa2:
        if(psg_index==14){
            joy_reads++;
            unsigned n=(PSG_readReg(psg,15)>>6)&1;
            return joy[n]|0xc0;
        }
        return PSG_readReg(psg,psg_index);
    case 0xa8:return primary;
    case 0xa9:key_reads++;return keys[ppi_c&15];
    case 0xaa:return ppi_c;
    case 0xab:return ppi_mode;
    default:return 0xff;
    }
}
static void __not_in_flash_func(port_write)(void *ctx,uint16_t port,uint8_t v){
    (void)ctx;
    switch(port&255){
    case 0x98:tms9918_write(&vdp,false,v);break;
    case 0x99:tms9918_write(&vdp,true,v);irq_update();break;
    case 0xa0:psg_index=v&15;break;
    case 0xa1:
        audio_to(run_base+cpu.cycles);PSG_writeReg(psg,psg_index,v);psg_writes++;break;
    case 0xa8:primary=v;map_pages();break;
    case 0xaa:ppi_c=v;break;
    case 0xab:
        if(v&0x80){ppi_mode=v;primary=ppi_c=0;map_pages();}
        else {unsigned bit=(v>>1)&7;ppi_c=(ppi_c&~(1u<<bit))|((v&1)<<bit);}
        break;
    }
}
static void matrix_press(unsigned index){if(index<128)keys[index>>3]&=(uint8_t)~(1u<<(index&7));}
static int hid_matrix(unsigned k){
    if(k>=0x04&&k<=0x1d)return 22+k-4;
    if(k>=0x1e&&k<=0x26)return k-0x1d;
    if(k>=0x3a&&k<=0x3e)return 53+k-0x3a;
    switch(k){
    case 0x27:return 0;case 0x28:case 0x58:return 63;case 0x29:return 58;
    case 0x2a:return 61;case 0x2b:return 59;case 0x2c:return 64;
    case 0x2d:return 10;case 0x2e:return 11;case 0x2f:return 14;
    case 0x30:return 17;case 0x31:return 12;case 0x33:return 15;
    case 0x34:return 16;case 0x35:return 13;case 0x36:return 18;
    case 0x37:return 19;case 0x38:return 20;case 0x39:return 51;
    case 0x48:return 60;case 0x49:return 66;case 0x4a:return 65;
    case 0x4c:return 67;case 0x4d:return 62;case 0x4f:return 71;
    case 0x50:return 68;case 0x51:return 70;case 0x52:return 69;
    default:return -1;
    }
}
void r8_controls(const r8_input *s){
    memset(keys,255,sizeof keys);joy[0]=joy[1]=0x3f;
    for(unsigned i=0;i<6;i++){int m=hid_matrix(s->keys[i]);if(m>=0)matrix_press((unsigned)m);}
    if(s->modifiers&0x22)matrix_press(48);
    if(s->modifiers&0x11)matrix_press(49);
    if(s->modifiers&0x44)matrix_press(50);
    for(unsigned n=0;n<2;n++){
        unsigned p=s->pads[n];
        const unsigned flags[]={R8_UP,R8_DOWN,R8_LEFT,R8_RIGHT,R8_A,R8_B};
        for(unsigned b=0;b<6;b++)if(p&flags[b])joy[n]&=(uint8_t)~(1u<<b);
    }

    if(s->pads[0]&R8_START)matrix_press(53);
    if(s->pads[0]&R8_SELECT)matrix_press(58);
    if(s->pads[0]&R8_X)matrix_press(63);
}
bool r8_init(void *bulk,size_t capacity,r8_audio_fn audio,r8_service_fn service){
    if(!bulk||capacity<2*PICTURE_BYTES)return false;
    for(unsigned i=0;i<256;i++)palette_pairs[i]=(uint32_t)display555[i&15]|((uint32_t)display555[i>>4]<<16);
    picture_base=bulk;picture=picture_base;completed_picture=picture_base+PICTURE_BYTES;emit=audio;service_cb=service;
    if(!psg)psg=PSG_new(CLOCK/2,RATE);
    if(!scc)scc=SCC_new(CLOCK,RATE);
    if(!psg||!scc){failure="Sound allocation failed";return false;}
    return true;
}
bool r8_load(const uint8_t *game,size_t size,const uint8_t *bios,size_t bios_size,const char *name){
    loaded=false;failure="";
    if(!psg||!scc){failure="Core not initialized";return false;}
    if(!bios||bios_size!=32768){failure="Need 32K cbios_main_msx1.rom";return false;}
    if(!game||size<8192||size>1048576||(size%8192)){
        failure="ROM must be 8K..1M, 8K aligned";return false;
    }
    unsigned header=0;
    if(game[0]!='A'||game[1]!='B'){
        if(size<0x4002||game[0x4000]!='A'||game[0x4001]!='B'){
            failure="Missing MSX AB cartridge header";return false;
        }
        header=0x4000;
    }
    mapper_type_t type=size<=49152?MAPPER_PLAIN:mapper_detect(game,(uint32_t)size);
    if(name){
        if(strstr(name,".ascii8."))type=MAPPER_ASCII8;
        if(strstr(name,".ascii16."))type=MAPPER_ASCII16;
        if(strstr(name,".konami."))type=MAPPER_KONAMI;
        if(strstr(name,".scc."))type=MAPPER_KONAMI_SCC;
        if(strstr(name,".plain."))type=MAPPER_PLAIN;
    }
    if(type==MAPPER_PLAIN&&size>65536){failure="Plain ROM exceeds 64K";return false;}
    system_rom=bios;memset(ram,0,sizeof ram);
    memset(&cpu,0,sizeof cpu);memset(&vdp,0,sizeof vdp);
    picture=picture_base;completed_picture=picture_base+PICTURE_BYTES;
    memset(picture_base,0,2*PICTURE_BYTES);memset(keys,255,sizeof keys);
    primary=ppi_c=psg_index=0;ppi_mode=0x82;joy[0]=joy[1]=0x3f;
    cpu_time=beam_time=audio_time=run_base=0;sample_phase=0;hp_x=hp_y=0;
    psg_writes=joy_reads=key_reads=pcm_samples=0;scc_bank=2;
    mapper_init(&cart,game,(uint32_t)size,type);
    unsigned entry=game[header+2]|(game[header+3]<<8);
    plain_base=size>32768||header?0:((size<=16384&&entry>=0x8000&&entry<0xc000)?0x8000:0x4000);

    memset(psg,0,sizeof *psg);psg->clk=CLOCK/2;PSG_setRate(psg,RATE);
    PSG_reset(psg);PSG_setVolumeMode(psg,2);
    memset(scc,0,sizeof *scc);scc->clk=CLOCK;SCC_set_rate(scc,RATE);
    SCC_reset(scc);SCC_set_type(scc,SCC_STANDARD);
    map_pages();halt_ticks=0;halt_spans=0;
    cpu.fetch_opcode=fetch_opcode;cpu.nop=halt_fetch;cpu.fetch=cpu.read=memory_read;
    cpu.write=memory_write;cpu.in=port_read;cpu.out=port_write;
    cpu.options=Z80_MODEL_ZILOG_NMOS;
    z80_power(&cpu,1);z80_instant_reset(&cpu);
    tms9918_register_interrupt_func(&vdp,irq_assert);
    printf("MSX_LOAD size=%u mapper=%s base=%04x entry=%04x clock=%u\n",(unsigned)size,mapper_name(type),(unsigned)plain_base,entry,CLOCK);
    loaded=true;return true;
}
void r8_frame(void){
    if(!loaded)return;
    for(int y=0;y<LINES;y++){

        beam_time+=LINE_CYCLES;
        if(cpu_time<beam_time){run_base=cpu_time;cpu_time+=z80_run(&cpu,(size_t)(beam_time-cpu_time));}
        audio_to(cpu_time);
        if(y<192){
            msx1_render_line_index(&vdp,line_pixels,y);

            for(unsigned x=0;x<256;x+=8){
                uint32_t word=line_pixels[x]|(line_pixels[x+1]<<4)|(line_pixels[x+2]<<8)|(line_pixels[x+3]<<12)|
                    (line_pixels[x+4]<<16)|(line_pixels[x+5]<<20)|(line_pixels[x+6]<<24)|(line_pixels[x+7]<<28);
                line_pixels[x/8]=word;
            }
            memcpy(picture+y*128,line_pixels,128);
        }
        if(y==191)check_and_generate_interrupt(&vdp);
        if((y&7)==7&&service_cb)service_cb();
    }

    completed_picture=picture;
    picture=picture==picture_base?picture_base+PICTURE_BYTES:picture_base;
}
void r8_render(uint16_t *out){

    uint8_t row[128];
    unsigned dy=0;
    for(unsigned sy=0;sy<192;sy++){
        memcpy(row,completed_picture+sy*128,sizeof row);
        uint16_t *dest=out+dy*320;
        if(((uintptr_t)dest&3)==0){
            typedef uint32_t __attribute__((__may_alias__)) pixel_pair;
            pixel_pair *pairs=(pixel_pair *)dest;
            for(unsigned x=0,d=0;x<128;x+=4,d+=5){
                unsigned a=row[x],b=row[x+1],c=row[x+2],e=row[x+3];
                pairs[d]=palette_pairs[(a&15)*17];
                pairs[d+1]=palette_pairs[(a>>4)|((b&15)<<4)];
                pairs[d+2]=palette_pairs[(b>>4)|((c&15)<<4)];
                pairs[d+3]=palette_pairs[c];pairs[d+4]=palette_pairs[e];
            }
        }else{
            for(unsigned dx=0;dx<320;dx++){
                unsigned sx=dx*4/5;
                dest[dx]=display555[(row[sx>>1]>>(4*(sx&1)))&15];
            }
        }
        dy++;
        if((sy&3)==0){memcpy(out+dy*320,dest,320*sizeof(uint16_t));dy++;}
    }
}
const char *r8_error(void){return failure;}
uint32_t r8_clock(void){return CLOCK;}
uint32_t r8_frame_cycles(void){return LINE_CYCLES*LINES;}
uint16_t r8_pc(void){return Z80_PC(cpu);}
uint8_t r8_peek(uint16_t address){return ram[address];}
void r8_restore_save(const uint8_t *p,size_t n){(void)p;(void)n;}
size_t r8_export_save(uint8_t *p,size_t n){(void)p;(void)n;return 0;}
void msx1_stats(msx1_statistics *s){*s=(msx1_statistics){cpu_time,beam_time,pcm_samples,psg_writes,joy_reads,key_reads};}
#ifdef MSX1_TEST
uint8_t msx1_test_in(uint8_t p){return port_read(NULL,p);}
void msx1_test_out(uint8_t p,uint8_t v){port_write(NULL,p,v);}
uint8_t msx1_test_read(uint16_t a){return memory_read(NULL,a);}
void msx1_test_write(uint16_t a,uint8_t v){memory_write(NULL,a,v);}
void msx1_test_sprite(unsigned i,uint8_t out[4]){memcpy(out,vdp.vram+((vdp.registers[5]&127)*128)+(i&31)*4,4);}
#endif
