#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
static int64_t fake_ms;
static int fake_clock(clockid_t id, struct timespec *t)
{ (void)id; t->tv_sec = fake_ms / 1000; t->tv_nsec = (fake_ms % 1000) * 1000000; return 0; }
#define clock_gettime fake_clock
#define BT_SERVICE 1
#define BT_BRIDGE 1
#define BT_HID_CAPTURE 1
#include "../../src/bluetooth/bt_session.c"
#undef clock_gettime

static bt_hci_event_fn event_fn, acl_fn;
static void (*tick_fn)(void *);
static int (*cancel_fn)(void *);
static void *callback_context, *control_context;
static int radio_open, opens, closes, cleanup_polls, reject_connect, bad_profile, bad_switch_profile, switch_no_ack, early_classic;
static unsigned next_handle = 0x40, current_wire_handle;
static uint8_t report_map[1024];
static size_t report_map_size;
static struct { int acl; size_t size; uint8_t data[160]; } messages[512];
static unsigned message_head, message_tail;
static unsigned manager_ticks, accepted[BT_MAX_CONTROLLERS];
static uint8_t saved_classic_address[6];
static int switch_encryption_readback;
static unsigned encryption_reads;
static int known_classic(const uint8_t *address, void *context)
{ (void)context; return !memcmp(address, saved_classic_address, 6); }

void log_line(const char *fmt, ...) { (void)fmt; }
void notify(const char *fmt, ...) { (void)fmt; }
void log_poll(void) {}
int log_stop_requested(void) { return 0; }
int current_user(uint32_t *user) { *user = 1; return 1; }
static void manager_control(void *unused) { (void)unused; ++manager_ticks; }
static int manager_cancelled(void *unused) { (void)unused; return 0; }
static void input_report(const unsigned char *report, unsigned long n, unsigned long delivered, void *context)
{ (void)report; (void)n; accepted[(uintptr_t)context] = (unsigned)delivered; }
static void push(int is_acl, const uint8_t *data, size_t size)
{
    assert(message_tail - message_head < 512 && size <= sizeof messages[0].data);
    unsigned n = message_tail++ % 512;
    messages[n].acl = is_acl; messages[n].size = size; memcpy(messages[n].data, data, size);
}
static void completed(unsigned handle)
{ uint8_t p[7] = {0x13,5,1,0,0,1,0}; put(p+3,handle); push(0,p,sizeof p); }
static void incoming(unsigned handle, unsigned cid, const uint8_t *data, size_t size)
{
    uint8_t body[140], wire[144]; assert(size <= sizeof body - 4);
    put(body,(unsigned)size); put(body+2,cid); memcpy(body+4,data,size);
    for (size_t at=0;at<size+4;) {
        size_t count=size+4-at; if(count>27)count=27;
        put(wire,handle|(at?0x1000:0)); put(wire+2,(unsigned)count);
        memcpy(wire+4,body+at,count);push(1,wire,count+4);at+=count;
    }
}
static void notification(unsigned handle, unsigned buttons, unsigned axis)
{
    uint8_t p[19]={0x1b,2,0};
    for(unsigned i=0;i<8;i+=2)put(p+3+i,32768);
    put(p+3,axis);put(p+16,buttons);incoming(handle,4,p,sizeof p);
}
void bt_hci_set_handlers(bt_hci_event_fn e, bt_hci_event_fn a, void *c)
{ event_fn=e;acl_fn=a;callback_context=c; }
void bt_hci_set_control(void (*t)(void *), int (*c)(void *), void *x)
{ tick_fn=t;cancel_fn=c;control_context=x; }
int bt_hci_open(void) { assert(!radio_open);radio_open=1;++opens;return 1; }
int bt_hci_open_acl(void) { assert(radio_open);return 1; }
void bt_hci_close(void) { if(radio_open)++closes;radio_open=0;message_head=message_tail=0; }
int bt_hci_poll(unsigned ms, bt_hci_event_fn fn, void *context)
{
    assert(radio_open);fake_ms+=ms?ms:1;
    if(tick_fn)tick_fn(control_context);
    if(message_head==message_tail)return 0;
    unsigned n=message_head++%512;
    if(messages[n].acl)acl_fn(messages[n].data,messages[n].size,callback_context);
    else if(fn)fn(messages[n].data,messages[n].size,context);
    else event_fn(messages[n].data,messages[n].size,callback_context);
    return 1;
}
int bt_hci_cleanup_poll(unsigned ms) { ++cleanup_polls;return bt_hci_poll(ms,NULL,NULL); }
int bt_hci_send(unsigned op, const void *params, size_t size)
{
    (void)size;const uint8_t *p=params;assert(radio_open);
    if(op==0x0405) {
        assert(size==13 && p[6]==0x18 && p[7]==0xcc && p[8]==1 && p[9]==0 && p[12]==1);
        uint8_t request[12]={4,10}; memcpy(request+2,p,6); request[11]=1; push(0,request,12);
        uint8_t event[13]={3,11,0}; current_wire_handle=next_handle++; put(event+3,current_wire_handle);
        memcpy(event+5,p,6);event[11]=1;push(0,event,13);
    } else if(op==0x0411) {
        uint8_t req[8]={0x17,6}; memcpy(req+2,current->target,6);push(0,req,8);
    } else if(op==0x040b) {
        assert(!memcmp(p+6,current->classic.key,16));
        uint8_t done[5]={6,3,0};put(done+3,current->handle);push(0,done,5);
    } else if(op==0x040c) {
        uint8_t io[8]={0x31,6}; memcpy(io+2,current->target,6);push(0,io,8);
        uint8_t confirmation[12]={0x33,10};memcpy(confirmation+2,current->target,6);push(0,confirmation,12);
    } else if(op==0x042c) {
        uint8_t key[25]={0x18,23};memcpy(key+2,current->target,6);memset(key+8,0x55,16);key[24]=4;push(0,key,25);
        uint8_t done[5]={6,3,0};put(done+3,current->handle);push(0,done,5);
    } else if(op==0x0413) {
        if(!switch_encryption_readback) {
            uint8_t event[6]={8,4,0,p[0],p[1],1};push(0,event,6);
        }
    } else if(op==0x200d) {
        if(reject_connect){uint8_t status[6]={0x0f,4,0x0c,1,0x0d,0x20};push(0,status,6);return 1;}
        uint8_t event[21]={0x3e,19,1,0,0,0,0,0};
        current_wire_handle=next_handle++;put(event+4,current_wire_handle);event[7]=p[5];memcpy(event+8,p+6,6);push(0,event,21);
    } else if(op==0x2019) {
        uint8_t event[6]={8,4,0,p[0],p[1],1};push(0,event,6);
    } else if(op==0x0406) {
        uint8_t event[6]={5,4,0,p[0],p[1],0x13};push(0,event,6);
    }
    return 1;
}
int bt_hci_command(unsigned op, const void *params, size_t size, unsigned char *reply, size_t capacity)
{
    (void)params;(void)size;assert(capacity>=7 && radio_open);
    if(tick_fn)tick_fn(control_context);
    /* The first Xbox must still submit during the second's command waits. */
    if(links[0].ready && current!=&links[0]) {
        notification(links[0].handle,1,0);
        for(int i=0;i<2;++i)bt_hci_poll(1,NULL,NULL);
    }
    memset(reply,0,capacity);
    if(op==0x1009) {
        if(early_classic && current->target_type==BT_ADDRESS_CLASSIC) {
            uint8_t incoming_request[12]={4,10};memcpy(incoming_request+2,current->target,6);incoming_request[11]=1;push(0,incoming_request,12);
            uint8_t connected[13]={3,11,0};put(connected+3,next_handle++);memcpy(connected+5,current->target,6);connected[11]=1;push(0,connected,13);
            bt_hci_poll(1,NULL,NULL);bt_hci_poll(1,NULL,NULL);
        }
        return 7;
    }
    if(op==0x1005){reply[1]=128;reply[4]=4;return 8;}
    if(op==0x0c55){reply[1]=1;return 2;}
    if(op==0x1408){
        assert(size==2 && word(params)==current->handle);++encryption_reads;
        reply[0]=switch_encryption_readback==2?0x2f:0;
        put(reply+1,current->handle+(switch_encryption_readback==3));
        reply[3]=switch_encryption_readback==4?7:16;
        return 4;
    }
    if(op==0x2002){reply[1]=27;reply[3]=1;return 4;}
    return 1;
}
int bt_hci_cleanup_command(unsigned op, const void *params, size_t size, unsigned char *reply, size_t capacity)
{ return bt_hci_command(op,params,size,reply,capacity); }
int bt_hci_acl_send(const unsigned char *packet, size_t size)
{
    assert(radio_open && size>=8 && outstanding_credit()==0);
    unsigned id=word(packet)&0xfff; const uint8_t *p=packet+8;
    assert((word(packet)>>12)==0);completed(id);
    uint8_t response[128];size_t n=0;
    if(current->target_type==BT_ADDRESS_CLASSIC) {
        unsigned cid=word(packet+6);
        if(cid==1) {
            if(p[0]==2){
                unsigned local=word(p+6); uint8_t r[12]={3,p[1],8,0};put(r+4,local+0x20);put(r+6,local);incoming(id,1,r,12);
                uint8_t config[12]={4,0x71,8,0};put(config+4,local);config[8]=1;config[9]=2;put(config+10,128);incoming(id,1,config,12);
            } else if(p[0]==4){uint8_t r[10]={5,p[1],6,0};put(r+4,word(p+4)-0x20);incoming(id,1,r,10);}
            else assert(p[0]==5 || p[0]==1 || p[0]==3);
        } else {
            assert(cid==0x61 && p[0]==0xa2 && p[1]==1);
            assert(size-8==(p[11]==2?12:p[11]==0x10?17:13));
            if(p[11]==0x40)assert(p[12]==1);
            uint8_t r[50]={0xa1,0x21};r[14]=0x80;r[15]=p[11];
            if(p[11]==2){r[18]=bad_switch_profile?1:3;incoming(id,0x41,r,50);}
            else if(!switch_no_ack) {
                if(p[11]==0x10){r[16]=0x3d;r[17]=0x60;r[20]=18;}
                incoming(id,0x41,r,50);
                if(p[11]==0x03){uint8_t full[50]={0xa1,0x30};incoming(id,0x41,full,50);}
            }
        }
        return 1;
    }
    if(p[0]==2){response[0]=3;response[1]=128;response[2]=0;n=3;}
    else if(p[0]==0x10){
        if(word(p+1)==1){uint8_t v[]={0x11,6,1,0,4,0,0x12,0x18};memcpy(response,v,sizeof v);n=sizeof v;}
        else {uint8_t v[]={1,0x10,p[1],p[2],0x0a};memcpy(response,v,sizeof v);n=sizeof v;}
    } else if(p[0]==4){uint8_t v[]={5,1,1,0,0x4b,0x2a,2,0,0x4d,0x2a,3,0,8,0x29,4,0,2,0x29};memcpy(response,v,sizeof v);n=sizeof v;}
    else if((p[0]==0x0a||p[0]==0x0c)&&word(p+1)==1){
        size_t offset=p[0]==0x0c?word(p+3):0;
        assert(offset<report_map_size);n=report_map_size-offset;if(n>127)n=127;
        response[0]=p[0]==0x0c?0x0d:0x0b;memcpy(response+1,report_map+offset,n);++n;
        if(bad_profile&&offset==0)response[8]^=1;
    } else if(p[0]==0x0a&&word(p+1)==3){response[0]=0x0b;response[1]=1;response[2]=1;n=3;}
    else if(p[0]==0x12){response[0]=0x13;n=1;}
    else assert(0);
    incoming(id,4,response,n);return 1;
}

/* Exercise the real virtual-pad API and translator through simulated ioctls. */
static struct { int open, deleted, submits, busy; uint8_t report[VPAD_REPORT_SIZE]; } pads[24];
static unsigned pad_count;
int __real_open(const char *,int,...);
int __real_close(int);
int __wrap_open(const char *path,int flags,...)
{
    if(!strcmp(path,"/dev/hid")){assert(pad_count<24);pads[pad_count].open=1;return 100+(int)pad_count++;}
    if (flags & O_CREAT) {
        va_list args; va_start(args, flags); int mode = va_arg(args, int); va_end(args);
        return __real_open(path, flags, mode);
    }
    return __real_open(path,flags);
}
int __wrap_close(int fd)
{ if(fd>=100){assert(pads[fd-100].open);pads[fd-100].open=0;return 0;}return __real_close(fd); }
int __wrap_ioctl(int fd,unsigned long op,...)
{
    assert(fd>=100);unsigned i=(unsigned)fd-100;assert(pads[i].open);
    va_list args;va_start(args,op);void *data=va_arg(args,void *);va_end(args);
    if(op==0xC018482AUL){struct request{uint32_t cmd,reserved;void *data,*out;} *r=data;*(int32_t *)r->out=1000+(int)i;return 0;}
    if(op==0x8010482CUL){struct request{uint32_t handle,reserved;void *data;} *r=data;assert(r->handle==1000+i);
        if(pads[i].busy){errno=EBUSY;return -1;}++pads[i].submits;memcpy(pads[i].report,r->data,VPAD_REPORT_SIZE);return 0;}
    assert(op==0x80104850UL);++pads[i].deleted;return 0;
}
static unsigned bits(unsigned pad_id)
{ uint32_t b;memcpy(&b,pads[pad_id].report+12,4);return b; }
static void poll_messages(void)
{
    for(unsigned i=0;i<300 && message_head!=message_tail;++i)assert(bt_sessions_poll(1)==0);
    for(unsigned i=0;i<5;++i)assert(bt_sessions_poll(1)==0);
}
static void bond(char *path,uint8_t value)
{
    strcpy(path,"/tmp/xinput-multi-XXXXXX");int fd=mkstemp(path);assert(fd>=0);
    uint8_t record[64]={0};memcpy(record,"XBSC0001",8);record[15]=record[54]=value;record[62]=16;
    assert(write(fd,record,64)==64);close(fd);
}
int main(void)
{
    FILE *f=fopen("testing_tools/tests/xbox-series-bt-map.hex","r");assert(f);unsigned byte;
    while(fscanf(f,"%2x",&byte)==1)report_map[report_map_size++]=(uint8_t)byte;
    fclose(f);
    char paths[BT_MAX_CONTROLLERS][64];
    for (unsigned i=0;i<BT_MAX_CONTROLLERS;++i) bond(paths[i],(uint8_t)(0x11*(i+1)));
    bt_sessions_set_control(manager_control,manager_cancelled,NULL);
    bt_session_config configs[BT_MAX_CONTROLLERS]={0};
    for(unsigned i=0;i<BT_MAX_CONTROLLERS;++i){configs[i].address[0]=(uint8_t)(0x11*(i+1));configs[i].bond_path=paths[i];configs[i].input=input_report;configs[i].context=(void *)(uintptr_t)i;}
    assert(bt_session_start(0,&configs[0])==1 && bt_session_connected(0));
    unsigned first_handle=links[0].handle;
    assert(bt_session_start(1,&configs[1])==1 && bt_session_connected(1));
    assert(opens==1 && closes==0 && links[1].handle!=first_handle);
    for(unsigned i=2;i<BT_MAX_CONTROLLERS;++i) assert(bt_session_start(i,&configs[i])==1 && bt_session_connected(i));
    assert(opens==1 && closes==0);
    assert(!bt_session_start(BT_MAX_CONTROLLERS,&configs[0]));
    notification(links[2].handle,8,32768);notification(links[3].handle,0x1010,32768);poll_messages();
    assert(bits(2)==0x8000 && bits(3)==0x11000);
    bt_session_disconnect(3);
    assert(bt_session_connected(0)&&bt_session_connected(1)&&bt_session_connected(2)&&pads[3].deleted==1&&closes==0);
    assert(bt_session_start(3,&configs[3])==1);
    assert(accepted[0]>1 && manager_ticks>10); /* Input while peer setup waits. */
    assert(!bt_session_start(1,&configs[0])); /* Occupied slots and duplicate IDs. */
    notification(first_handle,0x1001,0);notification(links[1].handle,2,65535);poll_messages();
    assert(bits(0)==0x14000 && bits(1)==0x2000);
    assert(pads[0].report[16]==0 && pads[1].report[16]==255);
    notification(first_handle,0,32768);poll_messages();assert(bits(0)==0 && bits(1)==0x2000);
    /* Interleaved fragments must never mix reassembly between handles. */
    uint8_t one[15]={0},two[15]={0},end[16]={0};
    put(one,first_handle);put(one+2,11);put(one+4,19);put(one+6,4);one[8]=0x1b;one[9]=2;
    put(two,links[1].handle);put(two+2,11);memcpy(two+4,one+4,11);
    put(end,first_handle|0x1000);put(end+2,12);put(end+13,8);push(1,one,15);push(1,two,15);push(1,end,16);
    put(end,links[1].handle|0x1000);put(end+13,16);push(1,end,16);poll_messages();
    assert(bits(0)==0x8000 && bits(1)==0x1000);
    /* Busy assignment on one pad must not block the other. */
    pads[1].busy=1;notification(links[1].handle,1,32768);notification(first_handle,2,32768);poll_messages();
    assert(links[1].was_busy && bits(0)==0x2000);pads[1].busy=0;fake_ms+=101;bt_sessions_poll(1);assert(bits(1)==0x4000);
    bt_session_disconnect(1);assert(bt_session_connected(0)&&!bt_session_connected(1)&&closes==0&&pads[1].deleted==1);
    /* Rejected initiation and unsupported profiles preserve the first link. */
    reject_connect=1;assert(bt_session_start(1,&configs[1])==3);reject_connect=0;
    assert(bt_session_connected(0)&&radio_open&&closes==0);
    bad_profile=1;assert(bt_session_start(1,&configs[1])==0);bad_profile=0;
    assert(bt_session_connected(0)&&radio_open&&pads[0].deleted==0);
    assert(bt_session_start(1,&configs[1])==1);unsigned second_pad=pad_count-1;
    notification(links[1].handle,0xc00,32768);poll_messages();fake_ms+=2001;bt_sessions_poll(1);
    assert(!bt_session_connected(1)&&bt_session_result(1)==2&&bt_session_connected(0)&&pads[second_pad].deleted==1);
    assert(bt_session_start(1,&configs[1])==1);
    int before=cleanup_polls;uint8_t native[12]={4,10};native[11]=1;push(0,native,12);
    assert(bt_sessions_poll(1)==4 && !radio_open && cleanup_polls==before);
    assert(!bt_session_connected(0)&&!bt_session_connected(1)&&pads[0].deleted==1);
    for(unsigned i=0;i<3;++i) assert(bt_session_start(i,&configs[i])==1);
    unsigned xbox_pads[3]; for(unsigned i=0;i<3;++i)xbox_pads[i]=pad_count-3+i;
    bt_session_config sw=configs[3];sw.address_type=BT_ADDRESS_CLASSIC;sw.fresh_pair=1;
    unsigned old_count=pad_count;
    bad_switch_profile=1;assert(bt_session_start(3,&sw)==0);bad_switch_profile=0;
    assert(pad_count==old_count && bt_session_connected(0) && bt_session_connected(1) && bt_session_connected(2));
    switch_no_ack=1;assert(bt_session_start(3,&sw)==0);switch_no_ack=0;
    assert(pad_count==old_count && bt_session_connected(0));
    assert(bt_session_start(3,&sw)==1 && bt_session_connected(3));
    unsigned switch_pad=pad_count-1;
    uint8_t full[50]={0xa1,0x30};full[4]=4;full[5]=0x10;full[6]=0x82;
    for(unsigned i=7;i<=10;i+=3){full[i]=0xd0;full[i+1]=7;full[i+2]=0x7d;}
    incoming(links[3].handle,0x41,full,50);notification(links[1].handle,2,32768);poll_messages();
    assert(bits(switch_pad)==(0x4000|0x10|0x100|0x10000));
    assert(pads[switch_pad].report[16]==128 && pads[switch_pad].report[17]==127 && pads[switch_pad].report[20]==255);
    assert(bits(xbox_pads[1])==0x2000);
    full[4]=full[5]=full[6]=0;incoming(links[3].handle,0x41,full,50);poll_messages();assert(bits(switch_pad)==0);
    uint8_t simple[13]={0xa1,0x3f,1,0x10,1,0,128,0,128,0,128,0,128};
    incoming(links[3].handle,0x41,simple,sizeof simple);poll_messages();
    assert(bits(switch_pad)==(0x4000|0x10000|0x10|0x20));
    simple[2]=simple[3]=0;simple[4]=8;simple[5]=simple[6]=0;simple[7]=simple[8]=255;
    incoming(links[3].handle,0x41,simple,sizeof simple);poll_messages();
    assert(bits(switch_pad)==0 && pads[switch_pad].report[16]==0 && pads[switch_pad].report[17]==255);
    assert(bt_session_connected(0)&&bt_session_connected(1)&&bt_session_connected(2));
    uint8_t saved[64];assert(bt_store_read(paths[3],saved) && !memcmp(saved,"XBCL0001",8) && saved[60]==2);
    bt_session_disconnect(3);assert(bt_session_connected(0)&&bt_session_connected(1)&&bt_session_connected(2));
    sw.fresh_pair=0;early_classic=1;assert(bt_session_start(3,&sw)==1);early_classic=0;
    /* Missing Encryption Change is recoverable only from affirmative radio
     * readback for this handle; negative/mismatched/short-key replies fail. */
    bt_session_disconnect(3);switch_encryption_readback=1;
    assert(bt_session_start(3,&sw)==1 && encryption_reads==1);
    bt_session_disconnect(3);
    for(int mode=2;mode<=4;++mode) {
        switch_encryption_readback=mode;unsigned count=pad_count;
        assert(bt_session_start(3,&sw)==0 && pad_count==count);
        assert(bt_session_connected(0)&&bt_session_connected(1)&&bt_session_connected(2));
    }
    switch_encryption_readback=0;
    assert(bt_session_start(3,&sw)==1);
    /* A saved Switch powering on between attempts must not disconnect Xbox.
     * Unknown/native requests still hand the radio back to the system. */
    bt_session_disconnect(3);
    memcpy(saved_classic_address,sw.address,6);
    bt_sessions_set_classic_known(known_classic,NULL);
    uint8_t wake[12]={4,10};memcpy(wake+2,sw.address,6);wake[11]=1;
    push(0,wake,sizeof wake);assert(bt_sessions_poll(1)==0);
    assert(bt_session_connected(0)&&bt_session_connected(1)&&bt_session_connected(2));
    notification(links[0].handle,2,32768);poll_messages();assert(bits(xbox_pads[0])==0x2000);
    push(0,native,sizeof native);assert(bt_sessions_poll(1)==4 && !radio_open);
    bt_sessions_set_classic_known(NULL,NULL);
    bt_sessions_close();
    for(unsigned i=0;i<pad_count;++i)assert(!pads[i].open&&pads[i].deleted==1);
    for(unsigned i=0;i<BT_MAX_CONTROLLERS;++i) unlink(paths[i]);
    puts("Four-Xbox and three-Xbox plus Switch Bluetooth integration passed: shared radio, real HID setup, peer input during setup, interleaved fragments, Guide, busy retry, independent disconnect/failure, native yield and cleanup.");
}
