#include "vpad.h"
#include "gip.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef VPAD_RUNTIME_TEST
#include <stdlib.h>
static uint32_t detected_firmware;
static int runtime_wide;
static unsigned firmware_reads, opened;
uint32_t kernel_get_fw_version(void) { ++firmware_reads; return detected_firmware; }
#endif

void log_line(const char *format, ...) { (void)format; }
int __wrap_open(const char *path, int flags, ...)
{ (void)flags; assert(!strcmp(path,"/dev/hid"));
#ifdef VPAD_RUNTIME_TEST
  ++opened;
#endif
  return 70; }
int __wrap_close(int fd) { assert(fd==70); return 0; }
static unsigned submitted, deleted;
static const unsigned char *expected_report;
int __wrap_ioctl(int fd, unsigned long op, ...)
{
    assert(fd==70);
    va_list ap; va_start(ap,op); unsigned char *request=va_arg(ap,void *); va_end(ap);
    uint32_t handle; memcpy(&handle,request,4);
    if(op==0xc018482aUL) {
        void *descriptor; int32_t *output;
        memcpy(&descriptor,request+8,8); memcpy(&output,request+16,8);
        const unsigned char *d=descriptor; uint32_t user;
        memcpy(&user,d+0x1c,4);
        assert(handle==3 && d[0]==3 && d[0x20]==1 && user==123);
        *output=42; return 0;
    }
    if(op==0x80104850UL) {
        uint32_t h;memcpy(&h,request+8,4);assert(request[0]==5 && h==42);
        ++deleted;return 0;
    }
#ifdef VPAD_RUNTIME_TEST
    assert(op==(runtime_wide?0x8018482cUL:0x8010482cUL));
    if(runtime_wide)for(unsigned i=16;i<24;++i)assert(request[i]==0);
#elif defined(VPAD_FW_1360)
    assert(op==0x8018482cUL);
    for(unsigned i=16;i<24;++i) assert(request[i]==0); /* flags and padding */
#else
    assert(op==0x8010482cUL);
#endif
    assert(handle==42);
    for(unsigned i=4;i<8;++i) assert(request[i]==0);
    const void *data;memcpy(&data,request+8,8);
    assert(data==expected_report); ++submitted;return 0;
}
int main(int argc, char **argv)
{
#ifdef VPAD_RUNTIME_TEST
    assert(argc==3);
    detected_firmware=(uint32_t)strtoul(argv[1],NULL,0);
    runtime_wide=atoi(argv[2]);
    vpad_input initial;
    vpad_input_init(&initial); /* Translation can precede registration. */
    if(runtime_wide<0) {
        char why[100];assert(!vpad_open(123,why,sizeof why));
        assert(!opened && firmware_reads==1 && strstr(why,"Cannot detect"));
        puts("Invalid firmware rejected before opening /dev/hid");return 0;
    }
#else
    (void)argc;(void)argv;
#endif
    char why[80];vpad *pad=vpad_open(123,why,sizeof why);assert(pad && vpad_handle(pad)==42);
    uint8_t gip[18]={GIP_LL_INPUT_REPORT,0,0,14};
    unsigned buttons=GIP_A|GIP_UP|GIP_LB|GIP_MENU;
    gip[4]=(uint8_t)buttons;gip[5]=(uint8_t)(buttons>>8);
    gip[6]=0xfc;gip[7]=3;gip[10]=0xff;gip[11]=0x7f;
    struct {unsigned char before[8], report[VPAD_REPORT_SIZE], after[8];} buffer;
    memset(&buffer,0x5a,sizeof buffer);
    vpad_translate(gip,buffer.report);
    for(unsigned i=0;i<8;++i) assert(buffer.before[i]==0x5a && buffer.after[i]==0x5a);
    uint32_t actual;memcpy(&actual,buffer.report+12,4);assert(actual==(0x4000|0x10|0x400|8|0x100));
    assert(buffer.report[16]==255 && buffer.report[20]==255 && buffer.report[23]==1);
#ifdef VPAD_RUNTIME_TEST
    const unsigned motion=runtime_wide?0x78:0x80;
    assert(initial.report[motion+14]==0x80 && initial.report[motion+15]==0x3f);
#elif defined(VPAD_FW_1360)
    const unsigned motion=0x78;
#else
    const unsigned motion=0x80;
#endif
    for(unsigned i=0;i<VPAD_REPORT_SIZE;++i) {
        if(i>=12 && i<=21)continue;
        if(i==23)continue;
        if(i==motion+14){assert(buffer.report[i]==0x80);continue;}
        if(i==motion+15){assert(buffer.report[i]==0x3f);continue;}
        assert(buffer.report[i]==0);
    }
    expected_report=buffer.report;assert(vpad_submit(pad,buffer.report)==1 && submitted==1);
    vpad_close(pad);assert(deleted==1);
#ifdef VPAD_RUNTIME_TEST
    assert(firmware_reads==1 && opened==1);
#endif
    puts("Virtual-pad firmware request, descriptor, motion layout, input mapping and buffer bounds passed.");
}
