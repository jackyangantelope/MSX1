/* SPDX-License-Identifier: BSD-3-Clause */
#include "retro8.h"
#include "msx1_test.h"
#include "tms9918.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static unsigned long long samples,changes,nonzero,services;
static int16_t last;
static uint64_t pcm_hash=1469598103934665603ull;
static unsigned rises;
static FILE *pcm;
static void audio(int16_t l,int16_t r){
    assert(l==r);samples++;changes+=l!=last;nonzero+=l!=0;rises+=last<=0&&l>0;last=l;
    pcm_hash^=(uint8_t)l;pcm_hash*=1099511628211ull;
    pcm_hash^=(uint8_t)((uint16_t)l>>8);pcm_hash*=1099511628211ull;
    if(pcm){int16_t p[]={l,r};assert(fwrite(p,2,2,pcm)==2);}
}
static void service(void){services++;}
static uint8_t *read_file(const char *path,size_t *size){
    FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));
    *size=(size_t)ftell(f);rewind(f);uint8_t *p=malloc(*size);assert(p);
    assert(fread(p,1,*size,f)==*size);fclose(f);return p;
}
static uint64_t hash(const void *p,size_t n){
    uint64_t h=1469598103934665603ull;const uint8_t *b=p;
    while(n--){h^=*b++;h*=1099511628211ull;}return h;
}
static void snapshot(const char *prefix,int f,uint16_t *p){
    char name[512];snprintf(name,sizeof name,"%s-%04d.ppm",prefix,f);
    FILE *out=fopen(name,"wb");assert(out);fprintf(out,"P6\n320 240\n255\n");
    for(int i=0;i<320*240;i++){
        uint8_t rgb[]={(p[i]&31)*255/31,((p[i]>>5)&31)*255/31,((p[i]>>10)&31)*255/31};
        assert(fwrite(rgb,1,3,out)==3);
    }fclose(out);
}
static void audio_reset(void){samples=changes=nonzero=services=rises=0;last=0;pcm_hash=1469598103934665603ull;}
static void reg(unsigned r,unsigned v){msx1_test_out(0xa0,r);msx1_test_out(0xa1,v);}
static void device_tests(const uint8_t *bios,size_t bs){
    uint8_t *rom=malloc(65536);assert(rom);
    for(unsigned i=0;i<65536;i++)rom[i]=i/8192;
    rom[0]='A';rom[1]='B';rom[2]=0;rom[3]=0x40;
    const char *types[]={"test.ascii8.rom","test.ascii16.rom","test.konami.rom","test.scc.rom"};
    for(unsigned t=0;t<4;t++){
        assert(r8_load(rom,65536,bios,bs,types[t]));msx1_test_out(0xa8,0x55);
        if(t==0){for(unsigned i=0;i<4;i++){msx1_test_write(0x6000+i*0x800,4+i);assert(msx1_test_read(0x4123+i*8192)==4+i);}}
        if(t==1){msx1_test_write(0x6000,2);msx1_test_write(0x7000,3);for(unsigned i=0;i<4;i++)assert(msx1_test_read(0x4123+i*8192)==4+i);}
        if(t==2){assert(msx1_test_read(0x4123)==0);for(unsigned i=0;i<3;i++){msx1_test_write(0x6000+i*8192,5+i);assert(msx1_test_read(0x6123+i*8192)==5+i);}}
        if(t==3){for(unsigned i=0;i<4;i++){msx1_test_write(0x5000+i*8192,4+i);assert(msx1_test_read(0x4123+i*8192)==4+i);}}
    }

    msx1_test_out(0xa8,0xff);msx1_test_write(0,0x76);
    reg(0,254);reg(1,0);reg(7,0x3e);reg(8,15);audio_reset();
    for(unsigned i=0;i<60;i++)r8_frame();
    assert(rises>=438&&rises<=444);printf("AY_PITCH_PASS hz=%.3f rises=%u\n",rises*44100.0/samples,rises);
    assert(r8_load(rom,65536,bios,bs,"test.scc.rom"));msx1_test_out(0xa8,0xd7);msx1_test_write(0,0x76);
    msx1_test_write(0x9000,0x3f);
    for(unsigned i=0;i<32;i++){uint8_t v=i<16?100:(uint8_t)-100;msx1_test_write(0x9800+i,v);assert(msx1_test_read(0x9800+i)==v);}
    msx1_test_write(0x9880,253);msx1_test_write(0x9881,0);msx1_test_write(0x988a,15);msx1_test_write(0x988f,1);
    audio_reset();for(unsigned i=0;i<60;i++)r8_frame();
    assert(rises>=438&&rises<=444);printf("SCC_PITCH_PASS hz=%.3f rises=%u\n",rises*44100.0/samples,rises);
    assert(r8_load(rom,65536,bios,bs,"test.scc.rom"));msx1_test_out(0xa8,0xd7);msx1_test_write(0,0x76);msx1_test_write(0x9000,0x3f);
    for(unsigned i=0;i<128;i++)assert(msx1_test_read(0x9800+i)==0);
    audio_reset();for(unsigned i=0;i<3;i++)r8_frame();assert(nonzero==0);
    free(rom);
    tms9918_context_t v={0};uint32_t row[256];
    v.registers[1]=0x48;v.registers[2]=6;v.registers[5]=0x36;
    v.vram[0x1b00]=208;v.vram[0]=0x24;v.vram[1]=0x60;v.registers[7]=8;
    tms9918_render_line(&v,row,0);assert(row[0]==0xff21c842&&row[3]==row[0]&&row[4]==0xff5455ed);
    tms9918_render_line(&v,row,4);assert(row[0]==0xffd4524d&&row[4]==0xfffc5554);
    check_and_generate_interrupt(&v);assert(tms9918_read(&v,true)&0x80);assert(!(tms9918_read(&v,true)&0xe0));
    puts("DEVICE_PASS ascii8 ascii16 konami scc_banks scc_wave_cold_reset silence multicolor status_ack");
    audio_reset();
}
static void units(const uint8_t *game,size_t size,const uint8_t *bios,size_t bs,const char *name){
    assert(!r8_load(game,1,bios,bs,name));assert(!r8_load(game,size,bios,bs-1,name));
    assert(r8_load(game,size,bios,bs,name));

    assert(msx1_test_read(0x8000)==0xff&&msx1_test_read(0xffff)==0xff);
    msx1_test_out(0xa8,0x55);
    if(size==49152){assert(msx1_test_read(0)==game[0]);assert(msx1_test_read(0x4000)=='A');assert(msx1_test_read(0xbfff)==game[0xbfff]);assert(msx1_test_read(0xc000)==255);}
    r8_input in={0};in.pads[0]=R8_UP|R8_A;in.pads[1]=R8_RIGHT|R8_B;r8_controls(&in);
    msx1_test_out(0xa0,15);msx1_test_out(0xa1,0);msx1_test_out(0xa0,14);
    assert(msx1_test_in(0xa2)==0xee);
    msx1_test_out(0xa0,15);msx1_test_out(0xa1,0x40);msx1_test_out(0xa0,14);
    assert(msx1_test_in(0xa2)==0xd7);
    memset(&in,0,sizeof in);in.keys[0]=0x50;in.keys[1]=0x2c;in.modifiers=2;r8_controls(&in);
    msx1_test_out(0xaa,8);assert(msx1_test_in(0xa9)==0xee);
    msx1_test_out(0xaa,6);assert(msx1_test_in(0xa9)==0xfe);
    memset(&in,0,sizeof in);r8_controls(&in);assert(msx1_test_in(0xa9)==0xff);

    msx1_test_out(0x99,0xff);msx1_test_out(0x99,0x7f);
    msx1_test_out(0x98,0x12);msx1_test_out(0x98,0x34);
    msx1_test_out(0x99,0xff);msx1_test_out(0x99,0x3f);
    assert(msx1_test_in(0x98)==0x12);assert(msx1_test_in(0x98)==0x34);
    puts("UNIT_PASS bios_bounds cartridge_48k keyboard joystick_1_2 release vdp_prefetch_wrap");
}
int main(int argc,char **argv){
    if(argc!=6){fprintf(stderr,"game bios prefix idle|keyboard|pad frames\n");return 2;}
    size_t gs,bs;uint8_t *game=read_file(argv[1],&gs),*bios=read_file(argv[2],&bs);
    uint8_t *guard=malloc(2*1024*1024+128);assert(guard);memset(guard,0xa5,2*1024*1024+128);
    uint16_t *pixels=malloc(320*240*2);assert(pixels);
    assert(r8_init(guard+64,2*1024*1024,audio,service));device_tests(bios,bs);units(game,gs,bios,bs,argv[1]);
    assert(r8_load(game,gs,bios,bs,argv[1]));
    char file[512];snprintf(file,sizeof file,"%s.pcm",argv[3]);pcm=fopen(file,"wb");assert(pcm);
    int frames=atoi(argv[5]);assert(frames>=300);
    uint64_t boot_video=0,boot_audio=0,boot_samples=0;
    unsigned ship_start=0,ship_right=0;
    for(int f=0;f<frames;f++){
        r8_input in={0};unsigned p=0;
        if(strcmp(argv[4],"idle")){

            if(f>=360&&f<1500&&(f%120)<12)p=R8_A;

            if(f>=1500&&f<1512)p=R8_UP;
            if(f>=1560&&f<1572)p=R8_RIGHT;
            if(f>=1620&&f<1632)p=R8_A;
            if(f>=1800){p=R8_A;if(f%300<90)p|=R8_RIGHT;else if(f%300<180)p|=R8_LEFT;if(f%600>=450)p|=R8_UP;}
            if(!strcmp(argv[4],"keyboard")){
                int n=0;if(p&R8_A)in.keys[n++]=0x2c;if(p&R8_RIGHT)in.keys[n++]=0x4f;
                if(p&R8_LEFT)in.keys[n++]=0x50;
                if(p&R8_UP)in.keys[n++]=0x52;
            }else in.pads[0]=p;
        }
        r8_controls(&in);r8_frame();assert(!r8_error()[0]);

        if(strcmp(argv[4],"idle")){
            uint8_t sprite[4];msx1_test_sprite(4,sprite);

            if(f==1799){ship_start=sprite[1];assert(sprite[0]<180);}
            if(f==1859){ship_right=sprite[1];assert(ship_right>ship_start);printf("MOVE_RIGHT_PASS from=%u to=%u\n",ship_start,ship_right);}
            if(f==1979){assert(sprite[1]<ship_right);printf("MOVE_LEFT_PASS from=%u to=%u\n",ship_right,sprite[1]);}
        }
        if((f+1)%300==0||f==frames-1){
            r8_render(pixels);snapshot(argv[3],f+1,pixels);
            printf("CHECK frame=%d pc=%04x video=%016llx\n",f+1,r8_pc(),(unsigned long long)hash(pixels,320*240*2));
        }
        if(f==299){boot_video=hash(pixels,320*240*2);boot_audio=pcm_hash;boot_samples=samples;}
    }
    fclose(pcm);pcm=NULL;
    for(int i=0;i<64;i++)assert(guard[i]==0xa5&&guard[2*1024*1024+64+i]==0xa5);
    msx1_statistics st;msx1_stats(&st);
    assert(st.beam==(uint64_t)frames*r8_frame_cycles());assert(st.cycles>=st.beam&&st.cycles-st.beam<40);
    assert(st.samples==st.cycles*44100/r8_clock());assert(changes>100&&nonzero>100&&services>100);
    printf("RESULT mode=%s frames=%d cycles=%llu samples=%llu changes=%llu nonzero=%llu psg=%llu joy=%llu keys=%llu services=%llu video=%016llx\n",argv[4],frames,(unsigned long long)st.cycles,samples,changes,nonzero,(unsigned long long)st.psg_writes,(unsigned long long)st.joy_reads,(unsigned long long)st.key_reads,services,(unsigned long long)hash(pixels,320*240*2));

    assert(r8_load(game,gs,bios,bs,argv[1]));samples=changes=nonzero=services=0;last=0;
    pcm_hash=1469598103934665603ull;
    for(int f=0;f<300;f++){r8_input in={0};r8_controls(&in);r8_frame();}
    r8_render(pixels);printf("RELOAD frame300=%016llx samples=%llu\n",(unsigned long long)hash(pixels,320*240*2),samples);
    assert(hash(pixels,320*240*2)==boot_video&&pcm_hash==boot_audio&&samples==boot_samples);
    free(game);free(bios);free(pixels);free(guard);puts("RELOAD_OK");return 0;
}
