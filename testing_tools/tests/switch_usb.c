#include "switch_usb.h"
#include "vpad.h"
#include "gip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void log_line(const char *fmt, ...) { (void)fmt; }
static void pack(unsigned char *p, unsigned x, unsigned y)
{ p[0]=x; p[1]=(x>>8)|((y&15)<<4); p[2]=y>>4; }
static unsigned buttons(const vpad_input *v)
{ unsigned b; memcpy(&b,v->report+12,4); return b; }
static void init(switch_usb *p, int valid_cal)
{
    unsigned char out[64], ack[64]={0}; int64_t now=0;
    switch_usb_init(p);
    static const unsigned usb[]={2,3,2,4};
    for(int i=0;i<4;++i){
        assert(switch_usb_output(p,now,out)==2 && out[0]==0x80 && out[1]==usb[i]);
        /* A declined transfer must not advance state or consume a retry. */
        assert(!p->awaiting && p->retries==0);
        switch_usb_sent(p,now);
        if(i<3){
            assert(!switch_usb_output(p,now+500,out));
            ack[0]=0x81;ack[1]=0xff;switch_usb_receive(p,ack,2);
            assert(p->stage==i+1);
            ack[1]=usb[i];switch_usb_receive(p,ack,2);
        }
        now+=10;
    }
    assert(p->stage==5);
    assert(switch_usb_output(p,now,out)==64 && out[10]==0x10 && out[11]==0x3d && out[15]==18);
    assert(out[2]==0 && out[3]==1 && out[4]==0x40 && out[5]==0x40);
    switch_usb_sent(p,now);
    memset(ack,0,sizeof ack);ack[0]=0x21;ack[13]=0x90;ack[14]=0x10;
    ack[15]=0x3d;ack[16]=0x60;ack[19]=18;
    pack(ack+20,1600,1500);pack(ack+23,2100,2000);pack(ack+26,1400,1300);
    pack(ack+29,2050,2150);pack(ack+32,1450,1550);pack(ack+35,1550,1650);
    if(!valid_cal)memset(ack+20,255,18);
    switch_usb_receive(p,ack,37);assert(p->stage==5);
    ack[15]=0;switch_usb_receive(p,ack,38);assert(p->stage==5);
    ack[15]=0x3d;switch_usb_receive(p,ack,38);assert(p->stage==6);
    assert(p->calibrated==valid_cal);
    static const unsigned subcmds[]={0x40,3,0x48,0x30};
    for(int stage=6;stage<=9;++stage){
        now+=10;assert(switch_usb_output(p,now,out)==64);
        assert(out[10]==subcmds[stage-6] && out[11]==(stage==7?0x30:1));
        switch_usb_sent(p,now);ack[14]=out[10];ack[13]=0;
        switch_usb_receive(p,ack,15);assert(p->stage==stage);
        ack[13]=0x80;switch_usb_receive(p,ack,15);
    }
    assert(p->stage==10 && !switch_usb_output(p,now,out));
}
int main(void)
{
    switch_usb p;init(&p,1);
    unsigned char msg[64]={0x30},out[18];int home;
    vpad_input v;vpad_input_init(&v);
    pack(msg+6,2100,2000);pack(msg+9,2050,2150);
    assert(switch_usb_normalize(&p,msg,49,out,&home) && !home);
    assert(vpad_input_update(&v,out,18));
    assert(v.report[16]==128 && v.report[17]==127 && v.report[18]==128 && v.report[19]==127);
    const struct{int byte,mask;unsigned ps;} cases[]={
        {3,1,0x8000},{3,2,0x1000},{3,4,0x4000},{3,8,0x2000},
        {3,0x40,0x800},{3,0x80,0x200},{4,1,0x100000},{4,2,8},
        {4,4,4},{4,8,2},{4,0x10,0x10000},{4,0x20,0x100000},
        {5,1,0x40},{5,2,0x10},{5,4,0x20},{5,8,0x80},{5,0x40,0x400},{5,0x80,0x100}
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];++i){
        msg[3]=msg[4]=msg[5]=0;msg[cases[i].byte]=cases[i].mask;
        assert(switch_usb_normalize(&p,msg,49,out,&home));v.guide=home;
        assert(vpad_input_update(&v,out,18) && buttons(&v)==cases[i].ps);
    }
    msg[3]=msg[4]=msg[5]=0;
    pack(msg+6,4095,0);pack(msg+9,0,4095);
    assert(switch_usb_normalize(&p,msg,49,out,&home));v.guide=home;
    assert(vpad_input_update(&v,out,18));
    assert(v.report[16]==255 && v.report[17]==255 && v.report[18]==0 && v.report[19]==0);
    assert(!(buttons(&v)&0x10000)); /* Home release cannot remain stuck. */
    /* Actual factory calibration and untouched report from the FW 4.03 test. */
    const unsigned char factory[]={0xea,0x85,0x63,0xa3,0x58,0x7f,0x54,0x36,0x62,
                                  0x63,0x37,0x82,0xdb,0x25,0x65,0x03,0x16,0x61};
    unsigned char reply[64]={0x21};reply[13]=0x90;reply[14]=0x10;
    reply[15]=0x3d;reply[16]=0x60;reply[19]=18;memcpy(reply+20,factory,18);
    switch_usb_init(&p);p.stage=5;p.awaiting=1;switch_usb_receive(&p,reply,64);
    assert(p.calibrated);p.stage=10;
    const unsigned char idle[]={0xa6,0xd8,0x79,0x96,0xb7,0x80};
    memcpy(msg+6,idle,6);
    assert(switch_usb_normalize(&p,msg,64,out,&home));v.guide=home;
    assert(vpad_input_update(&v,out,18));
    assert(v.report[16]==128 && v.report[17]==127 && v.report[18]==128 && v.report[19]==127);
    assert(!switch_usb_normalize(&p,msg,12,out,&home));
    msg[0]=0x81;assert(!switch_usb_normalize(&p,msg,49,out,&home));
    init(&p,0);assert(p.axes[0].center==2000 && p.axes[0].below==1500);
    switch_usb_init(&p);unsigned char wire[64];
    for(int i=0;i<3;++i){assert(switch_usb_output(&p,i*1000,wire)==2);switch_usb_sent(&p,i*1000);}
    assert(!switch_usb_output(&p,3000,wire) && p.failed);
    assert(!switch_usb_normalize(&p,msg,49,out,&home));
    switch_usb_init(&p);p.stage=5;
    for(int i=0;i<3;++i){assert(switch_usb_output(&p,i*1000,wire)==64);switch_usb_sent(&p,i*1000);}
    assert(switch_usb_output(&p,3000,wire)==64 && p.stage==6 && !p.failed);
    puts("Switch USB passed: acknowledged handshake, bounded retries, calibration validation, physical button mapping, axes, digital triggers and Home edges.");
    return 0;
}
