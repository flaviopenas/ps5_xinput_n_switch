#define _DEFAULT_SOURCE
#include "bt_hci.h"
#include "bt_session.h"
#include "log.h"
#include "usb_service.h"
static unsigned long usb_ticks;
int usb_service_init(void) { return 1; }
int usb_service_poll(void) { ++usb_ticks; return 0; }
int usb_service_status(usb_controller_status out[USB_MAX_CONTROLLERS])
{
    for (int i = 0; i < USB_MAX_CONTROLLERS; ++i)
        out[i] = (usb_controller_status){i+1,1,"Test USB controller",usb_ticks,usb_ticks,0};
    return USB_MAX_CONTROLLERS;
}
void usb_service_close(void) {}
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
void bt_wipe(void *p, size_t n) { volatile unsigned char *v = p; while (n--) *v++ = 0; }
void log_line(const char *fmt, ...) { (void)fmt; }
void notify(const char *fmt, ...) { (void)fmt; }
int log_open_port(const char *d, const char *p, unsigned port) { (void)d; (void)p; (void)port; return 1; }
void log_close(void) {}
void log_poll(void) {}
int log_stop_requested(void) { return 0; }
static void (*tick_fn)(void *);
static int (*cancel_fn)(void *);
static void *control_context;
void bt_hci_set_control(void (*t)(void *), int (*c)(void *), void *x) { tick_fn=t; cancel_fn=c; control_context=x; }
void bt_hci_set_handlers(bt_hci_event_fn e, bt_hci_event_fn a, void *x) { (void)e; (void)a; (void)x; }
int bt_hci_send(unsigned op, const void *p, size_t n) { (void)op; (void)p; (void)n; return 1; }
int bt_hci_open(void) { return 1; }
void bt_hci_close(void) {}
int bt_hci_command(unsigned op, const void *p, size_t n, unsigned char *r, size_t cap)
{ (void)op; (void)p; (void)n; if (!cap) return -1; r[0]=0; return 1; }
int bt_hci_cleanup_command(unsigned op, const void *p, size_t n, unsigned char *r, size_t cap)
{ return bt_hci_command(op,p,n,r,cap); }
int bt_hci_poll(unsigned ms, bt_hci_event_fn e, void *x)
{ (void)e; (void)x; if(tick_fn)tick_fn(control_context); usleep(ms*1000); return cancel_fn && cancel_fn(control_context) ? 0 : 1; }
static bt_session_config sessions[BT_MAX_CONTROLLERS];
static int session_up[BT_MAX_CONTROLLERS], session_results[BT_MAX_CONTROLLERS];
void bt_sessions_set_control(void (*t)(void *), int (*c)(void *), void *x) { bt_hci_set_control(t,c,x); }
void bt_sessions_set_advertisement(void (*a)(const bt_advert *, void *), void *x) { (void)a; (void)x; }
int bt_sessions_open(void) { return 1; }
int bt_session_start(unsigned slot, const bt_session_config *c)
{
    if (slot >= BT_MAX_CONTROLLERS || session_up[slot]) return 0;
    sessions[slot] = *c; session_up[slot] = 1; session_results[slot] = 1;
    c->stage("Connected",c->context);
    unsigned char report[24]={0}; report[16]=slot ? 50 : 200; report[17]=128; report[18]=128; report[19]=128;
    c->input(report,42+slot,41+slot,c->context);
    return 1;
}
int bt_session_connected(unsigned slot) { return slot < BT_MAX_CONTROLLERS && session_up[slot]; }
int bt_session_result(unsigned slot) { return slot < BT_MAX_CONTROLLERS ? session_results[slot] : 0; }
void bt_session_disconnect(unsigned slot) { if (slot < BT_MAX_CONTROLLERS) session_up[slot] = 0; }
void bt_sessions_close(void) { for (unsigned i=0;i<BT_MAX_CONTROLLERS;++i) session_up[i]=0; }
int bt_sessions_poll(unsigned ms)
{
    if(tick_fn)tick_fn(control_context);
    usleep(ms*1000);
    const char *yield_file = getenv("XINPUT_TEST_NATIVE_REQUEST");
    if (yield_file && access(yield_file, F_OK) == 0) {
        unlink(yield_file);
        for (unsigned i=0;i<BT_MAX_CONTROLLERS;++i) { session_results[i]=4; session_up[i]=0; }
        return 4;
    }
    const char *drop_file = getenv("XINPUT_TEST_DROP_SLOT");
    if (drop_file && access(drop_file,F_OK)==0) {
        FILE *file=fopen(drop_file,"r"); unsigned slot;
        if(file){ if(fscanf(file,"%u",&slot)==1 && slot<BT_MAX_CONTROLLERS){session_up[slot]=0;session_results[slot]=0;} fclose(file); }
        unlink(drop_file);
    }
    return 0;
}
