#include "shell_bind_shim.h"
#include "shell_bind.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum scenario {SUCCESS, DENIED_ATTACH, DENIED_WRITE, MISSING_SYMBOL, BAD_PROTECTION,
               UNEXPECTED_STOP, TIMEOUT, SERVICE_ERROR, OTHER_THREAD_STOP, MISSING_VECTOR_STATE,
               DENIED_PTRACE_READ, NO_READ_METHOD, SHORT_READ, DENIED_PTRACE_WRITE,
               WRITE_NOT_APPLIED, PARTIAL_STACK_WRITE, NO_DEBUG_STATE, DEBUG_IN_USE,
               DENIED_DEBUG_ARM, DEBUG_NOT_APPLIED, FAILED_DEBUG_RESTORE, WRONG_STACK, WRONG_DEBUG_CAUSE,
               EXTRA_STOP};
static enum scenario scenario;
static int phase, attached, attach_calls, continued, detached, ticks;
static struct reg registers, initial;
static struct fpreg floating, initial_fp;
static unsigned char code;
static uint64_t stack_word, auth;
static uint8_t caps[16];
static unsigned char extended[1024];
static struct dbreg debugging, initial_debug;

void log_line(const char *fmt, ...) { (void)fmt; }
void log_poll(void) {}
void notify(const char *fmt, ...) { (void)fmt; }
int sysctl(const int *mib, unsigned count, void *buffer, size_t *size,
           const void *value, size_t value_size)
{
    struct kinfo_proc record = {sizeof record, 77, "SceShellUI"};
    (void)mib; (void)count; (void)value; (void)value_size;
    if (buffer) memcpy(buffer, &record, sizeof record);
    *size = sizeof record;
    return 0;
}
uint64_t kernel_get_ucred_authid(pid_t pid) { (void)pid; return auth; }
int kernel_get_ucred_caps(pid_t pid, uint8_t *out) { (void)pid; memcpy(out, caps, 16); return 0; }
int kernel_set_ucred_authid(pid_t pid, uint64_t value) { (void)pid; auth = value; return 0; }
int kernel_set_ucred_caps(pid_t pid, const uint8_t *value) { (void)pid; memcpy(caps, value, 16); return 0; }
int kernel_dynlib_handle(pid_t pid, const char *name, uint32_t *out)
{ (void)pid; (void)name; *out = 3; return 0; }
intptr_t kernel_dynlib_dlsym(pid_t pid, uint32_t module, const char *name)
{ (void)pid; (void)module; (void)name; return scenario == MISSING_SYMBOL ? 0 : 0x6000; }
intptr_t kernel_dynlib_resolve(pid_t pid, uint32_t module, const char *nid)
{ (void)pid; (void)module; (void)nid; return 0; }
int kernel_get_vmem_protection(pid_t pid, intptr_t address, size_t size)
{ (void)pid; (void)size; (void)address; return scenario == BAD_PROTECTION ? -1 : PROT_READ | PROT_EXEC; }
void nid_encode(const char *name, char *out) { (void)name; strcpy(out, "test"); }
int mdbg_copyout(pid_t pid, intptr_t address, void *buffer, size_t size)
{
    assert(pid == 77);
    if (scenario == NO_READ_METHOD) { errno = EIO; return -1; }
    if (address == initial.r_rip) { assert(size == 1); memcpy(buffer, &code, size); }
    else { assert(size == sizeof stack_word); memcpy(buffer, &stack_word, size); }
    return 0;
}
int mdbg_copyin(pid_t pid, const void *buffer, intptr_t address, size_t size)
{
    assert(pid == 77);
    if (scenario == DENIED_WRITE) { errno = EFAULT; return -1; }
    if (scenario == WRITE_NOT_APPLIED) return 0;
    if (address == initial.r_rip) { assert(size == 1); memcpy(&code, buffer, size); }
    else {
        assert(size == sizeof stack_word);
        if (scenario == PARTIAL_STACK_WRITE && *(const uint64_t *)buffer == (uint64_t)initial.r_rip) {
            memcpy(&stack_word, buffer, 1); errno = EIO; return -1;
        }
        memcpy(&stack_word, buffer, size);
    }
    return 0;
}

long __syscall(long number, ...)
{
    va_list args;
    int request, pid, data;
    void *address;
    assert(number == SYS_ptrace);
    va_start(args, number);
    request = va_arg(args, int);
    pid = va_arg(args, int);
    address = va_arg(args, void *);
    data = va_arg(args, int);
    va_end(args);
    assert(pid == 77 || pid == 88 || pid == 99);
    (void)data;
    switch (request) {
    case PT_ATTACH:
        ++attach_calls;
        if (scenario == DENIED_ATTACH) { errno = EPERM; return -1; }
        attached = 1; phase = 1; return 0;
    case PT_DETACH: assert(attached); attached = 0; ++detached; return 0;
    case PT_LWPINFO:
        assert(pid == 77);
        ((struct ptrace_lwpinfo *)address)->pl_lwpid =
            scenario == OTHER_THREAD_STOP && phase == 4 ? 99 : 88;
        return 0;
    case PT_GETREGS: assert(pid == 88 || (pid == 99 && scenario == OTHER_THREAD_STOP)); memcpy(address, &registers, sizeof registers); return 0;
    case PT_SETREGS: assert(pid == 88); memcpy(&registers, address, sizeof registers); return 0;
    case PT_GETFPREGS: assert(pid == 88); memcpy(address, &floating, sizeof floating); return 0;
    case PT_SETFPREGS: assert(pid == 88); memcpy(&floating, address, sizeof floating); return 0;
    case PT_GETDBREGS:
        assert(pid == 88);
        if (scenario == NO_DEBUG_STATE) return EINVAL;
        memcpy(address, &debugging, sizeof debugging); return 0;
    case PT_SETDBREGS: {
        struct dbreg *value = address;
        assert(pid == 88);
        if (scenario == DENIED_DEBUG_ARM && (value->dr[7] & 1)) return EPERM;
        if (scenario == FAILED_DEBUG_RESTORE && !(value->dr[7] & 1)) return EPERM;
        if (scenario != DEBUG_NOT_APPLIED) memcpy(&debugging, value, sizeof debugging);
        return 0;
    }
    case PT_GETXSTATE_INFO:
        assert(pid == 88);
        if (scenario == MISSING_VECTOR_STATE) { errno = EINVAL; return -1; }
        ((struct ptrace_xstate_info *)address)->xsave_len = sizeof extended;
        return 0;
    case PT_GETXSTATE: assert(pid == 88 && data == sizeof extended); memcpy(address, extended, sizeof extended); return 0;
    case PT_SETXSTATE: assert(pid == 88 && data == sizeof extended); memcpy(extended, address, sizeof extended); return 0;
    case PT_IO: {
        struct ptrace_io_desc *io = address;
        void *region;
        if ((uintptr_t)io->piod_offs == (uintptr_t)initial.r_rip) {
            assert(io->piod_len == 1);
            region = &code;
        } else {
            assert((uintptr_t)io->piod_offs == (((uintptr_t)initial.r_rsp - 512) & ~(uintptr_t)15) - 8);
            assert(io->piod_len == sizeof stack_word);
            region = &stack_word;
        }
        if (io->piod_op == PIOD_READ_D || io->piod_op == PIOD_READ_I) {
            if (scenario == DENIED_PTRACE_READ || scenario == NO_READ_METHOD) {
                errno = EINVAL; return -1;
            }
            memcpy(io->piod_addr, region, io->piod_len);
            if (scenario == SHORT_READ) io->piod_len = 0;
        } else {
            if (scenario == DENIED_WRITE) { errno = EIO; return -1; }
            if (scenario == DENIED_PTRACE_WRITE || scenario == WRITE_NOT_APPLIED)
                return EFAULT; /* SDK raw error convention */
            if (scenario == PARTIAL_STACK_WRITE && region == &stack_word &&
                *(const uint64_t *)io->piod_addr == (uint64_t)initial.r_rip) {
                memcpy(region, io->piod_addr, 1); errno = EIO; return -1;
            }
            memcpy(region, io->piod_addr, io->piod_len);
        }
        return 0;
    }
    case PT_CONTINUE:
        ++continued;
        assert(registers.r_rip == 0x6000);
        assert((registers.r_rsp & 15) == 8);
        assert(registers.r_rdi == 0xf030d && registers.r_rsi == 0x1eb70483);
        assert(code == 0x90 && stack_word == (uint64_t)initial.r_rip);
        assert(debugging.dr[0] == (uint64_t)initial.r_rip && (debugging.dr[7] & 0xf0003) == 1);
        memset(&floating, 0xaa, sizeof floating);
        memset(extended, 0xbb, sizeof extended);
        phase = 3;
        return 0;
    default: assert(0); return -1;
    }
}
pid_t shell_test_waitpid(pid_t pid, int *status, int options)
{
    (void)options;
    if (phase == 1) { phase = 2; *status = (SIGSTOP << 8) | 0x7f; return pid; }
    if (phase == 3 && scenario != TIMEOUT) {
        if (scenario == EXTRA_STOP && continued == 1) {
            *status = (SIGSTOP << 8) | 0x7f; return pid;
        }
        phase = 4;
        registers.r_rip = scenario == UNEXPECTED_STOP ? 0x9999 : initial.r_rip;
        registers.r_rsp += scenario == WRONG_STACK ? 16 : 8;
        debugging.dr[6] = scenario == WRONG_DEBUG_CAUSE ? 0 : 1;
        registers.r_rax = scenario == SERVICE_ERROR ? -5 : 0;
        *status = ((scenario == UNEXPECTED_STOP ? SIGSEGV : SIGTRAP) << 8) | 0x7f;
        return pid;
    }
    if (phase == 5) { phase = 4; *status = (SIGSTOP << 8) | 0x7f; return pid; }
    return 0;
}
int shell_test_kill(pid_t pid, int sig)
{
    assert(pid == 77);
    if (sig == SIGCONT) { assert(!attached); return 0; }
    assert(sig == SIGSTOP); phase = 5; return 0;
}
int shell_test_clock(clockid_t id, struct timespec *out)
{
    (void)id; ticks += 50;
    out->tv_sec = ticks / 1000;
    out->tv_nsec = (ticks % 1000) * 1000000L;
    return 0;
}
int shell_test_sleep(unsigned duration) { (void)duration; return 0; }

int main(void)
{
    for (scenario = SUCCESS; scenario <= EXTRA_STOP; ++scenario) {
        uint8_t original_caps[16];
        memset(&initial, 0, sizeof initial);
        initial.r_rip = 0x5000; initial.r_rsp = 0x10000; initial.r_rax = 23;
        registers = initial;
        memset(&initial_fp, 0x55, sizeof initial_fp); floating = initial_fp;
        memset(extended, 0x22, sizeof extended);
        memset(original_caps, 0x33, sizeof original_caps); memcpy(caps, original_caps, 16);
        code = 0x90;
        stack_word = 0x12345678; auth = 0x9876;
        memset(&initial_debug, 0, sizeof initial_debug);
        initial_debug.dr[0] = 0x1234;
        initial_debug.dr[7] = scenario == DEBUG_IN_USE ? 1 : 0x400;
        debugging = initial_debug;
        phase = attached = attach_calls = continued = detached = ticks = 0;
        assert(shell_bind_device(0xf030d, 0x1eb70483) ==
               (scenario == SUCCESS || scenario == DENIED_PTRACE_READ || scenario == SHORT_READ ||
                scenario == DENIED_PTRACE_WRITE || scenario == EXTRA_STOP));
        assert(attached == (scenario == FAILED_DEBUG_RESTORE));
        assert(code == 0x90 && stack_word == 0x12345678);
        assert(auth == 0x9876 && memcmp(caps, original_caps, 16) == 0);
        if (scenario == FAILED_DEBUG_RESTORE) assert(continued == 1 && !detached);
        else assert(debugging.dr[0] == initial_debug.dr[0] && debugging.dr[7] == initial_debug.dr[7]);
        if (scenario == WRITE_NOT_APPLIED || scenario == PARTIAL_STACK_WRITE ||
            scenario == NO_DEBUG_STATE || scenario == DEBUG_IN_USE ||
            scenario == DENIED_DEBUG_ARM || scenario == DEBUG_NOT_APPLIED)
            assert(!continued && detached == 1);
        assert(memcmp(&registers, &initial, sizeof initial) == 0);
        assert(memcmp(&floating, &initial_fp, sizeof initial_fp) == 0);
        for (unsigned i = 0; i < sizeof extended; ++i) assert(extended[i] == 0x22);
        if (scenario == MISSING_SYMBOL || scenario == BAD_PROTECTION) assert(!attach_calls);
        if (scenario == DENIED_WRITE) assert(!continued && detached == 1);
        if (scenario == MISSING_VECTOR_STATE) assert(!continued && detached == 1);
        if (scenario == NO_READ_METHOD) assert(!continued && detached == 1);
    }
    puts("Shell cleanup simulation passed: thread-local breakpoint, debug state preservation, SDK fallbacks, partial writes, wrong thread/stack/trap and timeout.");
    return 0;
}
