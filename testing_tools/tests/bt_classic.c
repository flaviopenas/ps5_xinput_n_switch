#include "bt_classic.h"
#include "gip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
void log_line(const char *fmt, ...) { (void)fmt; }
static unsigned commands, packets, adverts;
static uint8_t last[128];
static unsigned last_opcode;
static bt_advert discovered;
static int command(unsigned op, const void *p, size_t n, void *ctx)
{ (void)p; (void)n; (void)ctx; ++commands; last_opcode=op; return 1; }
static int send(unsigned cid, const uint8_t *p, size_t n, void *ctx)
{ (void)ctx; assert(cid==1 && n<=sizeof last); memcpy(last,p,n); ++packets; return 1; }
static void advert(const bt_advert *r, void *ctx) { (void)ctx; discovered=*r; ++adverts; }
int main(void)
{
    uint8_t simple[12]={0x3f,0,0,8,0,128,0,128,0,128,0,128}, normalized[18]; int home;
    assert(bt_classic_normalize_simple(simple,12,normalized,&home) && !home);
    for(unsigned i=4;i<18;++i)assert(normalized[i]==0);
    static const unsigned expected[8]={GIP_A,GIP_B,GIP_X,GIP_Y,GIP_LB,GIP_RB,0,0};
    for(unsigned i=0;i<8;++i) {
        simple[1]=(uint8_t)(1u<<i);assert(bt_classic_normalize_simple(simple,12,normalized,&home));
        assert((normalized[4]|(unsigned)normalized[5]<<8)==expected[i]);
        assert(normalized[6]==(i==6?252:0) && normalized[8]==(i==7?252:0));
    }
    simple[1]=0;simple[2]=0x3f;assert(bt_classic_normalize_simple(simple,12,normalized,&home) && home);
    assert((normalized[4]|(unsigned)normalized[5]<<8)==(GIP_VIEW|GIP_MENU|GIP_LS|GIP_RS));
    simple[2]=0;
    static const unsigned hats[9]={GIP_UP,GIP_UP|GIP_RIGHT,GIP_RIGHT,GIP_RIGHT|GIP_DOWN,GIP_DOWN,GIP_DOWN|GIP_LEFT,GIP_LEFT,GIP_LEFT|GIP_UP,0};
    for(unsigned i=0;i<9;++i){simple[3]=(uint8_t)i;assert(bt_classic_normalize_simple(simple,12,normalized,&home));assert((normalized[4]|(unsigned)normalized[5]<<8)==hats[i]);}
    simple[3]=9;assert(!bt_classic_normalize_simple(simple,12,normalized,&home));simple[3]=8;
    for(size_t n=0;n<12;++n)assert(!bt_classic_normalize_simple(simple,n,normalized,&home));
    simple[4]=simple[5]=simple[6]=simple[7]=0;simple[8]=simple[9]=simple[10]=simple[11]=255;
    assert(bt_classic_normalize_simple(simple,12,normalized,&home));
    assert(normalized[10]==0 && normalized[11]==128 && normalized[12]==255 && normalized[13]==127);
    assert(normalized[14]==255 && normalized[15]==127 && normalized[16]==0 && normalized[17]==128);
    const uint8_t addr[6]={1,2,3,4,5,6}; bt_classic b;
    bt_classic_init(&b,addr); b.command=command; b.send=send;
    uint8_t io[8]={0x31,6,1,2,3,4,5,7};
    bt_classic_event(&b,io,sizeof io,0); assert(b.head==b.tail && commands==0);
    io[7]=6; bt_classic_event(&b,io,sizeof io,0); assert(b.tail==1 && commands==0);
    bt_classic_tick(&b,0); assert(commands==1 && last_opcode==0x042b);
    uint8_t key[25]={0x18,23,1,2,3,4,5,6}; key[24]=3;
    bt_classic_event(&b,key,sizeof key,0); assert(b.failed && !b.have_key);
    bt_classic_init(&b,addr); b.command=command; b.send=send; b.authenticated=b.encrypted=1;
    /* Controller-initiated control channel and configuration. */
    uint8_t request[8]={2,8,4,0,0x11,0,0x60,0};
    bt_classic_receive(&b,1,request,8); assert(b.channels[0].remote==0x60 && packets==2);
    uint8_t malformed[13]={4,9,9,0,0x40,0,0,0,1,2,128,0,1};
    bt_classic_receive(&b,1,malformed,13); assert(!b.channels[0].peer_configured);
    uint8_t unknown[12]={4,10,8,0,0x40,0,0,0,4,2,0,0};
    bt_classic_receive(&b,1,unknown,12); assert(last[0]==5 && last[8]==3 && !b.channels[0].peer_configured);
    uint8_t small_mtu[12]={4,11,8,0,0x40,0,0,0,1,2,47,0};
    bt_classic_receive(&b,1,small_mtu,12); assert(last[8]==1 && !b.channels[0].peer_configured);
    uint8_t config[12]={4,12,8,0,0x40,0,0,0,1,2,128,0};
    bt_classic_receive(&b,1,config,12); assert(last[8]==0 && b.channels[0].peer_configured);
    uint8_t response[10]={5,99,6,0,0x40,0};
    bt_classic_receive(&b,1,response,10); assert(!b.channels[0].configured);
    response[1]=(uint8_t)b.channels[0].config_id;
    bt_classic_receive(&b,1,response,10); assert(b.channels[0].configured);
    uint8_t eir[257]={0x2f,255,1,1,2,3,4,5,6};
    eir[11]=8; eir[12]=5; eir[16]=0xd8;
    eir[17]=15; eir[18]=9; memcpy(eir+19,"Pro Controller",14);
    assert(bt_classic_advert_parse(eir,sizeof eir,advert,NULL)==1);
    assert(adverts==1 && discovered.address_type==2 && discovered.hid_service && discovered.rssi==-40 && !strcmp(discovered.name,"Pro Controller"));
    for(size_t n=0;n<sizeof eir;++n) assert(bt_classic_advert_parse(eir,n,advert,NULL)<=0);
    assert(adverts==1);
    eir[17]=250; assert(bt_classic_advert_parse(eir,sizeof eir,advert,NULL)==-1 && adverts==1);
    uint8_t rssi[17]={0x22,15,1,1,2,3,4,5,6};rssi[11]=8;rssi[12]=5;rssi[16]=0xc4;
    assert(bt_classic_advert_parse(rssi,sizeof rssi,advert,NULL)==1 && discovered.rssi==-60 && discovered.hid_service);
    uint8_t name[257]={7,255,0,1,2,3,4,5,6};memcpy(name+9,"8BitDo Ultimate 2C",18);
    assert(bt_classic_advert_parse(name,sizeof name,advert,NULL)==1 && discovered.event_type==4);
    puts("Classic Bluetooth tests passed: selected-peer SSP replies, debug-key rejection, inbound HID configuration, MTU negotiation, malformed/unknown options, matching response IDs and bounded discovery parsing.");
}
