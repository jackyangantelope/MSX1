/* SPDX-License-Identifier: BSD-3-Clause */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/psram.h"
#include "hardware/sync.h"
#include "ff.h"
#include "tf_card.h"
#include "hstx.h"
#include "retro8.h"
#include "jack_input_devices.h"
#include "jack_tinyusb_input.h"
#include "jack_nespad_input.h"
#include "font_8x8.h"
#if R8_QUEUE_DIAG
#define R8_PROFILE_TX_CAPACITY 3072
#endif
#include "retro8_profile.h"
#if R8_QUEUE_DIAG
#include "retro8_queue_diag.h"
static r8_queue_diag queue_diag;
#endif
#if R8_DEFERRED_PRESENT
#include "retro8_present.h"
#endif
#if R8_ZX_SPLIT_PROFILE
uint32_t r8_zx_profile_now(void){return time_us_32();}
#endif
static FATFS fs;
static jack_input_queue queue;
static jack_input_devices devices;
static char names[48][96];
static int16_t samples[4096][2];
static unsigned rd,wr,count,names_count,selected;
static bool usb_ok,pad_ok,playing,buffering,memory_ok;
static uint64_t audio_next;
static uint32_t under,over,resync,slow,frames,max_frame_us;
#if N3_SAVE
static uint8_t saved_bytes[2048],save_scratch[2048];
static size_t saved_size;
static bool save_loaded;
static char save_path[160];
static void flush_save(void){
    if(!save_loaded)return;
    size_t n=r8_export_save(save_scratch,sizeof save_scratch);
    if(!n||(n==saved_size&&!memcmp(save_scratch,saved_bytes,n)))return;
    char temporary[168],backup[168];
    snprintf(temporary,sizeof temporary,"%s.tmp",save_path);
    snprintf(backup,sizeof backup,"%s.bak",save_path);
    FIL f;UINT written=0;FRESULT result=f_open(&f,temporary,FA_WRITE|FA_CREATE_ALWAYS);
    if(result==FR_OK){
        result=f_write(&f,save_scratch,n,&written);
        if(result==FR_OK&&written!=n)result=FR_DISK_ERR;
        if(result==FR_OK)result=f_sync(&f);
        FRESULT closed=f_close(&f);if(result==FR_OK)result=closed;
    }
    if(result==FR_OK){
        FILINFO info;bool existed=f_stat(save_path,&info)==FR_OK;
        if(existed){(void)f_unlink(backup);result=f_rename(save_path,backup);}
        if(result==FR_OK){
            result=f_rename(temporary,save_path);
            if(result!=FR_OK&&existed)(void)f_rename(backup,save_path);
        }
    }
    if(result==FR_OK){memcpy(saved_bytes,save_scratch,n);saved_size=n;}
    r8_profile_printf("N3_SAVE bytes=%lu result=%u\n",(unsigned long)n,result);
}
#endif
#if R8_DEFERRED_PRESENT
static r8_present_state presentation;
static bool in_frame,frame_work;
static uint32_t inside_submitted;
static uint64_t inside_render_us;
static uint32_t last_frame_work_us,render_max_us;

static bool try_present(bool idle) {
    if(!presentation.pending)return false;
    if(!hstx_framebuffer_available()){
        if(!idle)presentation.blocked++;
        return false;
    }
    uint64_t begin=time_us_64();
    r8_render((uint16_t *)hstx_getframebuffer());
    bool accepted=hstx_present_frame();
    uint32_t elapsed=(uint32_t)(time_us_64()-begin);
    r8_present_finish(&presentation,accepted,idle);
    r8_profile.render_us+=elapsed;
    if(accepted)r8_profile.shown++;
    if(elapsed>render_max_us)render_max_us=elapsed;
    if(in_frame&&accepted){inside_submitted++;inside_render_us+=elapsed;}
    if(idle&&!frame_work){

        r8_profile.work_us+=elapsed;last_frame_work_us+=elapsed;
        if(last_frame_work_us>max_frame_us)max_frame_us=last_frame_work_us;
    }
    return true;
}
#endif

static uint8_t *const bulk=(uint8_t *)0x11000000u;
static uint8_t *const rom=(uint8_t *)0x11200000u;
static uint8_t *const bios=(uint8_t *)0x11400000u;
static bool key(unsigned k){for(unsigned i=0;i<6;i++)if(devices.keyboard_keys[i]==k)return true;return false;}
static void __not_in_flash_func(service)(void){
#if R8_PROFILE
    uint64_t begin=time_us_64(),mark=begin;
#endif
    if(usb_ok)jack_tinyusb_input_poll();
#if R8_PROFILE
    uint64_t stamp=time_us_64();r8_profile.usb_us+=stamp-mark;mark=stamp;
#endif
#if R8_ASYNC_PAD
    if(pad_ok)(void)jack_nespad_input_service(time_us_32());
#else
    if(pad_ok)(void)jack_nespad_input_poll();
#endif
#if R8_PROFILE
    stamp=time_us_64();r8_profile.pad_us+=stamp-mark;
#endif
    jack_input_event e;while(jack_input_pop(&queue,&e)){}
#if R8_PROFILE
    mark=time_us_64();
#endif
#if R8_QUEUE_DIAG
    uint64_t audio_stamp=time_us_64();
    r8_queue_service_begin(&queue_diag,audio_stamp);
    uint64_t now=audio_stamp*44100u;
#else
    uint64_t now=time_us_64()*44100u;
#endif
    if(now>audio_next+1000000ull*2048){audio_next=now;resync++;}
#if R8_QUEUE_DIAG
    uint64_t due_start=audio_next;
    unsigned queue_before=count;
#endif
    if(buffering&&count>=1764){
        buffering=false;
#if R8_QUEUE_DIAG
        if(queue_diag.active)queue_diag.w.leave++;
#endif
    }

    uint32_t due=audio_next<=now?(uint32_t)((now-audio_next)/1000000u)+1u:0;
    audio_next+=(uint64_t)due*1000000u;
    while(due--){
        int l=0,r=0;
        if(playing&&!buffering){
            if(count){l=samples[rd][0];r=samples[rd][1];rd=(rd+1)&4095;count--;}
            else{
                under++;buffering=true;
#if R8_QUEUE_DIAG
                queue_diag.w.enter++;
                if(!queue_diag.w.first_under.valid)
                    r8_queue_capture(&queue_diag,false,time_us_64(),frames,count,r8_pc(),buffering);
#endif
            }
        }
        hstx_push_audio_sample(l,r);
    }
#if R8_QUEUE_DIAG
    r8_queue_service_end(&queue_diag,queue_before,count,(uint32_t)((audio_next-due_start)/1000000u));
#endif
#if R8_PROFILE
    r8_profile.audio_us+=time_us_64()-mark;r8_profile_pump();
#endif
#if R8_DEFERRED_PRESENT

    if(playing&&presentation.pending&&hstx_framebuffer_available())(void)try_present(true);
#endif
#if R8_PROFILE
    r8_profile.service_us+=time_us_64()-begin;r8_profile.service_calls++;
#endif
}
static void __not_in_flash_func(pcm)(int16_t l,int16_t r){
#if R8_QUEUE_DIAG
    queue_diag.w.offered++;
#endif
    if(count==4096){
        over++;
#if R8_QUEUE_DIAG
        queue_diag.w.dropped++;
        if(!queue_diag.w.first_over.valid)
            r8_queue_capture(&queue_diag,true,time_us_64(),frames,count,r8_pc(),buffering);
#endif
        return;
    }
    samples[wr][0]=l;samples[wr][1]=r;wr=(wr+1)&4095;count++;
#if R8_QUEUE_DIAG
    queue_diag.w.enqueued++;
    if(count>queue_diag.w.qmax)queue_diag.w.qmax=count;
#endif
}
static void text(uint16_t *p,unsigned x,unsigned y,const char *s,uint16_t color){
    for(;*s&&x+8<=320;s++,x+=8){
        unsigned c=(unsigned char)*s;if(c<32||c>126)c='?';
        for(unsigned r=0;r<8;r++)for(unsigned b=0;b<8;b++)
            if(font_8x8[r*95+c-32]&(1u<<b))p[(y+r)*320+x+b]=color;
    }
}
static void menu(const char *message){
    if(!hstx_framebuffer_available())return;
    uint16_t *p=(uint16_t *)hstx_getframebuffer();memset(p,0,320*240*2);
    text(p,8,12,R8_TITLE " 0.1.6",0x7fff);
    unsigned top=selected/9*9;
    for(unsigned n=top;n<names_count&&n<top+9;n++){
        text(p,8,36+(n-top)*18,n==selected?">":" ",0x7fff);
        text(p,24,36+(n-top)*18,names[n],n==selected?0x7fe0:0x5294);
    }
    text(p,8,202,message,0x7fff);
    text(p,8,220,"F12 / SELECT+START: menu",0x5294);hstx_present_frame();
}
static FRESULT scan(void){
    DIR d;FILINFO f;FRESULT r=f_opendir(&d,R8_DIRECTORY);names_count=0;selected=0;
    if(r!=FR_OK)return r;
    while((r=f_readdir(&d,&f))==FR_OK&&f.fname[0]){
        const char *ext=strrchr(f.fname,'.');
        if(!(f.fattrib&AM_DIR)&&ext&&!strcasecmp(ext,R8_EXTENSION)&&strcasecmp(f.fname,R8_BIOS)&&strlen(f.fname)<96&&names_count<48)
            strcpy(names[names_count++],f.fname);
    }
    f_closedir(&d);
    for(unsigned i=1;i<names_count;i++){
        char tmp[96];strcpy(tmp,names[i]);unsigned j=i;
        while(j&&strcmp(names[j-1],tmp)>0){strcpy(names[j],names[j-1]);j--;}
        strcpy(names[j],tmp);
    }
    return r;
}
static size_t read_file(const char *path,uint8_t *dest,size_t capacity){
    FIL f;UINT got;if(f_open(&f,path,FA_READ)!=FR_OK)return 0;
    FSIZE_t len=f_size(&f);bool ok=len>0&&len<=capacity;
    if(ok)ok=f_read(&f,dest,(UINT)len,&got)==FR_OK&&got==len;
    f_close(&f);return ok?(size_t)len:0;
}
static bool load(void){
    char path[160];snprintf(path,sizeof path,R8_DIRECTORY "/%s",names[selected]);
    size_t size=read_file(path,rom,1048640),bios_size=0;
    if(R8_BIOS[0])bios_size=read_file(R8_DIRECTORY "/" R8_BIOS,bios,32768);
#if N3_SAVE
    snprintf(save_path,sizeof save_path,R8_DIRECTORY "/%s.sav",names[selected]);
    saved_size=read_file(save_path,saved_bytes,sizeof saved_bytes);
    if(!saved_size){char backup[168];snprintf(backup,sizeof backup,"%s.bak",save_path);saved_size=read_file(backup,saved_bytes,sizeof saved_bytes);}
    r8_restore_save(saved_bytes,saved_size);
#endif
    bool ok=r8_load(rom,size,bios,bios_size,names[selected]);
#if N3_SAVE
    save_loaded=ok;
#endif
    r8_profile_printf("R8_LOAD file=%s bytes=%lu bios=%lu ok=%u error=%s\n",names[selected],(unsigned long)size,(unsigned long)bios_size,ok,r8_error());
    return ok;
}
int main(void){
    vreg_set_voltage(VREG_VOLTAGE_1_30);sleep_ms(10);set_sys_clock_khz(252000,true);stdio_init_all();
    bool present=psram_is_available();
    int params=present?psram_configure_params(PICO_DEFAULT_PSRAM_MAX_FREQ,PICO_DEFAULT_PSRAM_MAX_SELECT,PICO_DEFAULT_PSRAM_MIN_DESELECT):PICO_ERROR_NOT_FOUND;
    uint32_t irq=save_and_disable_interrupts();
    int reinit=params==PICO_OK?psram_reinitialize():params;restore_interrupts(irq);
    memory_ok=present&&reinit==PICO_OK&&psram_get_size()>=8*1024*1024;
    pico_fatfs_spi_config_t cfg={SDCARD_SPI,CLK_SLOW_DEFAULT,CLK_FAST_DEFAULT_PIO,SDCARD_PIN_MISO,SDCARD_PIN_CS,SDCARD_PIN_SCK,SDCARD_PIN_MOSI,true};
    if(!pico_fatfs_set_config(&cfg))panic("SD pins");
    FRESULT sd=f_mount(&fs,"",1);hstx_init(false);
    audio_next=time_us_64()*44100u;
    jack_input_init(&queue);jack_input_devices_init(&devices,&queue);
    usb_ok=jack_tinyusb_input_init(&devices);pad_ok=jack_nespad_input_init(&devices,252000);
    if(memory_ok)memory_ok=r8_init(bulk,2*1024*1024,pcm,service);
    FRESULT listing=sd==FR_OK?scan():sd;
    printf("R8_BOOT " R8_TITLE " " R8_BUILD_VERSION " Fruit Jam psram=%u size=%lu sd=%d count=%u usb=%u pad=%u\n",memory_ok,(unsigned long)psram_get_size(),listing,names_count,usb_ok,pad_ok);
#if R8_PROFILE || R8_SKIP_48K_AY || R8_ASYNC_PAD || R8_SRAM_HOT
    printf("R8_CFG variant=" R8_VARIANT " version=" R8_BUILD_VERSION " profile=%u async_pad=%u\n",R8_PROFILE,R8_ASYNC_PAD);
#if R8_DEFERRED_PRESENT
    printf("R8_PRESENT_CFG deferred=1 latest_only=1 blocking_wait=0\n");
#endif
#if R8_FUSED_TICK
    printf("R8_EXEC_CFG fused_tick=1 original_beeper=1\n");
#endif
#if R8_Z80_INLINE
    printf("R8_Z80_CFG inline_tick=1 all_states=1\n");
#if R8_HALT_BATCH
    printf("R8_HALT_CFG batch=1 event_ticks=original filter=exact\n");
#endif
#endif
#endif
#if R8_ZX_PACKED_RENDER
    printf("R8_RENDER_CFG packed=1 crop=320x240 pair_table=1024 original_palette=1\n");
#endif
#if R8_QUEUE_DIAG
    printf("R8_QUEUE_CFG diag=1 capacity=4096 refill=1764 uart_queue=3072 sample_changes=0\n");
#endif
#if R8_ZX_SPLIT_PROFILE
    printf("R8_ZX_CFG split=1 clock=us32 row_timing=1\n");
#endif
#if R8_BEEPER_STEADY
    printf("R8_BEEPER_CFG steady=1 window=128 exact_sum=1\n");
#endif
    unsigned old=0;const char *message="UP/DOWN + ENTER/A: play";
    uint64_t clock=r8_clock(),step=(uint64_t)r8_frame_cycles()*1000000;
    uint64_t next=time_us_64()*clock,report=time_us_64()+2000000;
#if R8_PROFILE
    r8_profile_reset(time_us_64());
#endif
    while(true){
        service();unsigned pad=devices.pads[0];
#if R8_DEFERRED_PRESENT
        if(playing&&try_present(true))service();
#endif
        unsigned buttons=(key(0x52)||(pad&R8_UP)?1:0)|(key(0x51)||(pad&R8_DOWN)?2:0)|
            (key(0x28)||(pad&R8_A)?4:0)|(key(0x45)||((pad&(R8_SELECT|R8_START))==(R8_SELECT|R8_START))?8:0);
        unsigned edge=buttons&~old;old=buttons;
        if(playing&&(edge&8)){
#if N3_SAVE
            flush_save();
#endif
#if R8_QUEUE_DIAG
            r8_queue_flush(&queue_diag,count);
#endif
            playing=false;rd=wr=count=0;message="UP/DOWN + ENTER/A: play";
#if R8_DEFERRED_PRESENT
            if(presentation.pending)presentation.replaced++;
            presentation.pending=false;
#endif
        }
        if(!playing){
            if(!memory_ok)message="PSRAM unavailable (Fruit Jam required)";
            else if(names_count){
                if(edge&1)selected=(selected+names_count-1)%names_count;
                if(edge&2)selected=(selected+1)%names_count;
                if(edge&4){
                    playing=load();message=playing?"":r8_error();rd=wr=count=0;buffering=playing;
                    audio_next=time_us_64()*44100u;next=time_us_64()*clock;
                    under=over=resync=slow=frames=max_frame_us=0;
#if R8_QUEUE_DIAG
                    r8_queue_reset(&queue_diag,time_us_64(),count,playing,buffering);
#endif
#if R8_DEFERRED_PRESENT
                    memset(&presentation,0,sizeof presentation);
                    last_frame_work_us=render_max_us=0;inside_submitted=0;inside_render_us=0;in_frame=false;frame_work=false;
#endif
#if R8_PROFILE
                    r8_profile_reset(time_us_64());report=time_us_64()+2000000;
#endif
                }
            }else message="No " R8_EXTENSION " files in " R8_DIRECTORY;
        }
        uint64_t now=time_us_64();
        if(now*clock>=next){
#if R8_QUEUE_DIAG
            r8_queue_due(&queue_diag,now,next,step,clock);
#endif
            next+=step;
            if(now*clock>next+step*2){
                next=now*clock;slow++;
#if R8_QUEUE_DIAG
                if(queue_diag.active)queue_diag.w.resets++;
#endif
            }
            if(playing){
#if R8_QUEUE_DIAG
                queue_diag.in_frame=true;
#endif
                r8_input state={0};memcpy(state.pads,devices.pads,sizeof state.pads);
                memcpy(state.keys,devices.keyboard_keys,sizeof state.keys);state.modifiers=devices.keyboard_modifier;
                r8_controls(&state);uint64_t begin=time_us_64();
#if R8_PROFILE
                uint64_t service_before=r8_profile.service_us;
#endif
                frame_work=true;in_frame=true;r8_frame();in_frame=false;
                if(r8_error()[0]){playing=false;message=r8_error();rd=wr=count=0;}
                uint64_t new_step=(uint64_t)r8_frame_cycles()*1000000;
                next=next-step+new_step;step=new_step;
#if R8_PROFILE
                r8_profile.emu_us+=time_us_64()-begin-(r8_profile.service_us-service_before);
#endif
#if R8_DEFERRED_PRESENT
                r8_present_offer(&presentation);
#endif
                service();
#if R8_DEFERRED_PRESENT
                (void)try_present(false);
#else
                if(hstx_framebuffer_available()){
#if R8_PROFILE
                    uint64_t render_begin=time_us_64();
#endif
                    r8_render((uint16_t *)hstx_getframebuffer());hstx_present_frame();
#if R8_PROFILE
                    r8_profile.render_us+=time_us_64()-render_begin;r8_profile.shown++;
#endif
                }
#endif
                service();uint32_t elapsed=(uint32_t)(time_us_64()-begin);frame_work=false;
#if R8_DEFERRED_PRESENT
                last_frame_work_us=elapsed;
#endif
                if(elapsed>max_frame_us)max_frame_us=elapsed;
                frames++;
#if R8_QUEUE_DIAG
                queue_diag.in_frame=false;frame_work=false;
#endif
#if R8_PROFILE
                r8_profile.work_us+=elapsed;r8_profile.completed++;
#endif
            }else menu(message);
        }
#if R8_QUEUE_DIAG
        else queue_diag.due_run=0;
#endif
        if(now>=report){
#if N3_SAVE
            if(playing)flush_save();
#endif
            service();
#if R8_PROFILE
            r8_profile_report(frames,max_frame_us,slow,count,under,over,resync,playing?r8_pc():0);
            extern void r8_msx_halt_get(uint64_t *,uint32_t *);
            uint64_t halt_ticks;uint32_t halt_spans;
            r8_msx_halt_get(&halt_ticks,&halt_spans);
            r8_profile_printf("R8_MSX_HALT ticks=%llu spans=%lu\n",(unsigned long long)halt_ticks,(unsigned long)halt_spans);
            r8_profile_printf("R8_MSX_SUBMIT inside=%lu render_us=%llu double_psram=1 index_bits=4 image_bytes=24576\n",(unsigned long)inside_submitted,(unsigned long long)inside_render_us);
#if R8_COLECO_HALT_BATCH
            uint64_t halt_ticks;uint32_t halt_spans;
            r8_coleco_halt_get(&halt_ticks,&halt_spans);
            r8_profile_printf("R8_CV_HALT skipped_ticks=%llu spans=%lu\n",
                (unsigned long long)halt_ticks,(unsigned long)halt_spans);
#endif
#if R8_DEFERRED_PRESENT
            r8_profile_printf("R8_PRESENT produced=%lu submitted=%lu blocked=%lu replaced=%lu deferred=%lu failed=%lu pending=%u render_max_us=%lu\n",
                (unsigned long)presentation.produced,(unsigned long)presentation.submitted,
                (unsigned long)presentation.blocked,(unsigned long)presentation.replaced,
                (unsigned long)presentation.deferred,(unsigned long)presentation.failed,
                (unsigned)presentation.pending,(unsigned long)render_max_us);
#endif
#if R8_QUEUE_DIAG
            r8_queue_report(&queue_diag,count,frames);
#endif
#if R8_SG_QUEUE_DIAG
            uint64_t sg_halt_ticks;uint32_t sg_halt_spans;
            r8_coleco_halt_get(&sg_halt_ticks,&sg_halt_spans);
            r8_profile_printf("R8_SG_HALT skipped_ticks=%llu spans=%lu\n",
                (unsigned long long)sg_halt_ticks,(unsigned long)sg_halt_spans);
#endif
#else
            printf("R8_PERF frames=%lu max_us=%lu slow=%lu audio_q=%u under=%lu over=%lu resync=%lu pc=%04x\n",(unsigned long)frames,(unsigned long)max_frame_us,(unsigned long)slow,count,(unsigned long)under,(unsigned long)over,(unsigned long)resync,playing?r8_pc():0);
#endif
            service();report=now+2000000;
        }
        sleep_us(50);
    }
}
