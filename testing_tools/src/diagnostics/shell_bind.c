#include "shell_bind.h"
#include "log.h"
#ifdef SHELL_BIND_HOST_TEST
#include "shell_bind_shim.h"
#else
#include <ps5/kernel.h>
#include <ps5/nid.h>
#include <ps5/mdbg.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <machine/reg.h>
#endif
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Design reference: OmniPad shellui_inject.c (GPL-3.0). This version uses the
 * stopped thread's RIP as a thread-local hardware return breakpoint, verifies the trap
 * address, preserves FP registers and stack bytes, and bounds stop waits.
 * No firmware-specific instruction offsets or permanent shell hooks. */
static int trace(int request, pid_t pid, void *address, int data)
{
    int result = (int)__syscall(SYS_ptrace, request, pid, address, data);
    /* These requests return zero on success. The SDK raw syscall stub leaves
     * FreeBSD errors as positive error numbers instead of setting errno. */
    if (result > 0) { errno = result; return -1; }
    return result;
}

static int memory(pid_t pid, int operation, uintptr_t address, void *buffer, size_t size)
{
    struct ptrace_io_desc io;
    int result, error;
    memset(&io, 0, sizeof io);
    io.piod_op = operation;
    io.piod_offs = (void *)address;
    io.piod_addr = buffer;
    io.piod_len = size;
    errno = 0;
    result = trace(PT_IO, pid, &io, 0);
    error = errno;
    if (result == 0 && io.piod_len == size) return 1;
    log_line("Shell memory: PT_IO op %d at 0x%llX, requested %lu, reported %lu, rc %d, errno %d (%s)",
             operation, (unsigned long long)address, (unsigned long)size,
             (unsigned long)io.piod_len, result, error, strerror(error));
    /* Some console debugger implementations reject instruction-space reads.
     * Retry using data-space reads, then the SDK debugger copy API. */
    if (operation == PIOD_READ_I) {
        io.piod_op = PIOD_READ_D;
        io.piod_len = size;
        if (trace(PT_IO, pid, &io, 0) == 0 && io.piod_len == size) {
            log_line("Shell memory: data-space read fallback succeeded");
            return 1;
        }
    }
    if (operation == PIOD_READ_I || operation == PIOD_READ_D) {
        errno = 0;
        result = mdbg_copyout(pid, (intptr_t)address, buffer, size);
        error = errno;
        log_line("Shell memory: SDK read fallback rc %d, errno %d (%s)", result, error, strerror(error));
        return result == 0;
    }
    errno = 0;
    result = mdbg_copyin(pid, buffer, (intptr_t)address, size);
    error = errno;
    log_line("Shell memory: SDK write fallback rc %d, errno %d (%s)", result, error, strerror(error));
    return result == 0;
}

static int write_memory(pid_t pid, int operation, uintptr_t address,
                        const void *buffer, size_t size)
{
    unsigned char observed[8];
    if (size > sizeof observed) return 0;
    if (!memory(pid, operation, address, (void *)buffer, size)) return 0;
    return memory(pid, operation == PIOD_WRITE_I ? PIOD_READ_I : PIOD_READ_D,
                  address, observed, size) && memcmp(observed, buffer, size) == 0;
}

static int restore_memory(pid_t pid, int read_op, int write_op,
                          uintptr_t address, const void *original, size_t size)
{
    unsigned char observed[8];
    if (size > sizeof observed) return 0;
    if (memory(pid, read_op, address, observed, size) &&
        memcmp(observed, original, size) == 0) return 1;
    if (!write_memory(pid, write_op, address, original, size)) return 0;
    return memory(pid, read_op, address, observed, size) &&
           memcmp(observed, original, size) == 0;
}

/* 1 stopped, 0 deadline, -1 exited/error. Never block in waitpid. */
static int wait_stopped(pid_t pid, int milliseconds, int *signal_out)
{
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        int status;
        pid_t found = waitpid(pid, &status, WNOHANG);
        if (found == pid) {
            if (!WIFSTOPPED(status)) return -1;
            *signal_out = WSTOPSIG(status);
            return 1;
        }
        if (found < 0 && errno != EINTR) return -1;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if ((now.tv_sec - start.tv_sec) * 1000 +
            (now.tv_nsec - start.tv_nsec) / 1000000 >= milliseconds) return 0;
        log_poll();
        usleep(5000);
    }
}

static pid_t shell_pid(void)
{
    int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0, offset = 0;
    unsigned char *buffer;
    pid_t selected = -1;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0 || !size || size > 8 * 1024 * 1024)
        return -1;
    size += 4096;
    buffer = malloc(size);
    if (!buffer) return -1;
    if (sysctl(mib, 4, buffer, &size, NULL, 0) != 0) { free(buffer); return -1; }
    while (offset + sizeof(int) <= size) {
        int stride;
        pid_t pid;
        char name[COMMLEN + 1];
        memcpy(&stride, buffer + offset, sizeof stride);
        if (stride < (int)(offsetof(struct kinfo_proc, ki_comm) + sizeof name) ||
            stride > 4096 || (size_t)stride > size - offset) break;
        memcpy(&pid, buffer + offset + offsetof(struct kinfo_proc, ki_pid), sizeof pid);
        memcpy(name, buffer + offset + offsetof(struct kinfo_proc, ki_comm), sizeof name);
        name[COMMLEN] = '\0';
        if (pid > 0 && strcmp(name, "SceShellUI") == 0) {
            if (selected > 0) { selected = -1; break; }
            selected = pid;
        }
        offset += (size_t)stride;
    }
    free(buffer);
    return selected;
}

int shell_bind_device(uint64_t device, uint32_t user)
{
#ifndef ENABLE_EXPERIMENTAL_SHELL_BIND
    /* A console error followed interrupted remote calls on FW 4.03. Keeping
     * this experimental path in the sources must not enable it by default. */
    log_line("Shell bind: disabled after FW 4.03 console error; device 0x%llX, user 0x%08X remains unassigned",
             (unsigned long long)device, user);
    return 0;
#endif
    pid_t pid = shell_pid(), me = getpid();
    uint32_t module = 0;
    intptr_t function;
    char nid[12];
    uint64_t auth;
    uint8_t caps[16], elevated[16];
    struct reg saved, call, finished;
    struct fpreg floating;
    struct dbreg debug_saved, debug_call, debug_finished;
    struct ptrace_xstate_info extended_info;
    void *extended = NULL;
    struct ptrace_lwpinfo thread_info;
    pid_t thread = -1;
    uint64_t original_stack = 0, return_address;
    uintptr_t stack_address = 0;
    int attached = 0, stopped = 0, saved_state = 0;
    int debug_changed = 0, stack_changed = 0, regs_changed = 0;
    int credentials_changed = 0, sig = 0, return_signal = 0, complete = 0, forced_stop = 0;
    int cleanup_ok = 1, waited;
    int protection;
    int64_t result = -1;

    if (pid <= 0) {
        log_line("Shell bind: SceShellUI not uniquely found; attempt skipped");
        return 0;
    }
    log_line("Shell bind: preflight for SceShellUI pid %d", pid);
    auth = kernel_get_ucred_authid(me);
    if (!auth || kernel_get_ucred_caps(me, caps) != 0) {
        log_line("Shell bind: cannot save payload credentials; attempt skipped");
        return 0;
    }
    memset(elevated, 0xff, sizeof elevated);
    credentials_changed = 1;
    if (kernel_set_ucred_authid(me, 0x4800000000010003ULL) != 0 ||
        kernel_set_ucred_caps(me, elevated) != 0) {
        log_line("Shell bind: temporary debugger credentials unavailable");
        goto cleanup;
    }
    if (kernel_dynlib_handle(pid, "libSceMbus", &module) != 0 || !module) {
        module = 0;
        if (kernel_dynlib_handle(pid, "libSceMbus.sprx", &module) != 0 || !module) {
            log_line("Shell bind: libSceMbus is not loaded in SceShellUI");
            goto cleanup;
        }
    }
    function = kernel_dynlib_dlsym(pid, module, "sceMbusBindDeviceWithUserId");
    if (!function) {
        nid_encode("sceMbusBindDeviceWithUserId", nid);
        function = kernel_dynlib_resolve(pid, module, nid);
    }
    protection = function > 0 ? kernel_get_vmem_protection(pid, function, 1) : -1;
    if (function <= 0 || protection < 0 || !(protection & PROT_EXEC)) {
        log_line("Shell bind: binding symbol missing or not executable");
        goto cleanup;
    }
    log_line("Shell bind: binding symbol at 0x%llX; attaching", (unsigned long long)function);
    if (trace(PT_ATTACH, pid, NULL, 0) != 0) {
        log_line("Shell bind: PT_ATTACH failed: %s", strerror(errno));
        goto cleanup;
    }
    attached = 1;
    waited = wait_stopped(pid, 1000, &sig);
    if (waited != 1) {
        log_line("Shell bind: attach stop not observed (%d)", waited);
        goto cleanup;
    }
    stopped = 1;
    if (sig != SIGSTOP) {
        return_signal = sig;
        log_line("Shell bind: unexpected attach signal %d; aborting", sig);
        goto cleanup;
    }
    memset(&thread_info, 0, sizeof thread_info);
    if (trace(PT_LWPINFO, pid, &thread_info, sizeof thread_info) != 0 ||
        thread_info.pl_lwpid <= 0) {
        log_line("Shell bind: stopped thread could not be identified");
        goto cleanup;
    }
    thread = thread_info.pl_lwpid;
    if (trace(PT_GETREGS, thread, &saved, 0) != 0 ||
        trace(PT_GETFPREGS, thread, &floating, 0) != 0) {
        log_line("Shell bind: cannot preserve complete register state");
        goto cleanup;
    }
    saved_state = 1;
    if (trace(PT_GETDBREGS, thread, &debug_saved, 0) != 0 || (debug_saved.dr[7] & 0xff)) {
        log_line("Shell bind: hardware debug state unavailable or already in use; call skipped");
        goto cleanup;
    }
    memset(&extended_info, 0, sizeof extended_info);
    if (trace(PT_GETXSTATE_INFO, thread, &extended_info, sizeof extended_info) != 0 ||
        extended_info.xsave_len < 512 || extended_info.xsave_len > 16384) {
        log_line("Shell bind: full vector-state preservation unavailable; call skipped");
        goto cleanup;
    }
    extended = malloc(extended_info.xsave_len);
    if (!extended || trace(PT_GETXSTATE, thread, extended, extended_info.xsave_len) != 0) {
        log_line("Shell bind: cannot save full vector state; call skipped");
        goto cleanup;
    }
    protection = kernel_get_vmem_protection(pid, saved.r_rip, 1);
    if (saved.r_rsp < 4096 || saved.r_rip <= 0 || saved.r_rip == function ||
        protection < 0 || !(protection & PROT_EXEC)) {
        log_line("Shell bind: stopped RIP/RSP not suitable for remote call");
        goto cleanup;
    }
    /* Respect the caller's red zone and SysV entry stack alignment (%16 == 8). */
    stack_address = ((saved.r_rsp - 512) & ~(uintptr_t)15) - 8;
    return_address = saved.r_rip;
    log_line("Shell bind: stopped thread %d RIP 0x%llX RSP 0x%llX, return slot 0x%llX",
             thread, (unsigned long long)saved.r_rip, (unsigned long long)saved.r_rsp,
             (unsigned long long)stack_address);
    if (!memory(pid, PIOD_READ_D, stack_address, &original_stack, sizeof original_stack)) {
        log_line("Shell bind: return stack slot could not be read; no writes made");
        goto cleanup;
    }
    /* DR0 execution breakpoint, local enable only, length/type zero. Unlike
     * an INT3 in shared text, another shell thread cannot hit this breakpoint. */
    debug_call = debug_saved;
    debug_call.dr[0] = saved.r_rip;
    debug_call.dr[6] &= ~(uint64_t)0xf;
    debug_call.dr[7] = (debug_saved.dr[7] & ~(uint64_t)0xf0003) | 0x401;
    debug_changed = 1;
    errno = 0;
    int debug_set = trace(PT_SETDBREGS, thread, &debug_call, 0), debug_error = errno;
    memset(&debug_finished, 0, sizeof debug_finished);
    int debug_get = trace(PT_GETDBREGS, thread, &debug_finished, 0);
    log_line("Shell bind: hardware arm set %d, errno %d (%s), get %d, DR0 0x%llX, DR7 0x%llX",
             debug_set, debug_error, strerror(debug_error), debug_get,
             (unsigned long long)debug_finished.dr[0], (unsigned long long)debug_finished.dr[7]);
    if (debug_set != 0 || debug_get != 0 ||
        debug_finished.dr[0] != debug_call.dr[0] ||
        (debug_finished.dr[7] & 0xf0003) != 1) {
        log_line("Shell bind: thread-local return breakpoint could not be armed; call skipped");
        goto cleanup;
    }
    log_line("Shell bind: thread-local hardware return breakpoint armed");
    stack_changed = 1;
    if (!write_memory(pid, PIOD_WRITE_D, stack_address, &return_address, sizeof return_address)) {
        log_line("Shell bind: remote return address write failed");
        goto cleanup;
    }
    call = saved;
    call.r_rip = (uint64_t)function;
    call.r_rsp = stack_address;
    call.r_rdi = device;
    call.r_rsi = user;
    call.r_rdx = call.r_rcx = call.r_r8 = call.r_r9 = 0;
    regs_changed = 1;
    if (trace(PT_SETREGS, thread, &call, 0) != 0) goto cleanup;
    log_line("Shell bind: calling for device 0x%llX, user 0x%08X (3s wait)",
             (unsigned long long)device, user);
    struct timespec call_start, call_now;
    int remaining = 3000, extra_stops = 0;
    clock_gettime(CLOCK_MONOTONIC, &call_start);
    if (trace(PT_CONTINUE, pid, (void *)1, 0) != 0) goto cleanup;
    stopped = 0;
    for (;;) {
        waited = wait_stopped(pid, remaining, &sig);
        if (waited != 1 || sig != SIGSTOP || extra_stops >= 2) break;
        stopped = 1;
        clock_gettime(CLOCK_MONOTONIC, &call_now);
        remaining = 3000 - (int)((call_now.tv_sec - call_start.tv_sec) * 1000 +
                    (call_now.tv_nsec - call_start.tv_nsec) / 1000000);
        if (remaining <= 0) break;
        ++extra_stops;
        log_line("Shell bind: additional SIGSTOP %d; continuing unchanged registers (%dms remaining)",
                 extra_stops, remaining);
        if (trace(PT_CONTINUE, pid, (void *)1, 0) != 0) goto cleanup;
        stopped = 0;
    }
    if (waited == 1) {
        int info_ok, regs_ok = 0, debug_ok = 0;
        stopped = 1;
        memset(&thread_info, 0, sizeof thread_info);
        memset(&finished, 0, sizeof finished);
        info_ok = trace(PT_LWPINFO, pid, &thread_info, sizeof thread_info) == 0;
        if (info_ok && thread_info.pl_lwpid > 0)
            regs_ok = trace(PT_GETREGS, thread_info.pl_lwpid, &finished, 0) == 0;
        if (info_ok && thread_info.pl_lwpid == thread)
            debug_ok = trace(PT_GETDBREGS, thread, &debug_finished, 0) == 0;
        log_line("Shell bind: stop signal %d, thread %d (expected %d), info %d, regs %d, RIP 0x%llX (expected 0x%llX), RSP 0x%llX, DR6 0x%llX, RAX 0x%llX",
                 sig, thread_info.pl_lwpid, thread, info_ok, regs_ok,
                 (unsigned long long)finished.r_rip, (unsigned long long)saved.r_rip,
                 (unsigned long long)finished.r_rsp,
                 (unsigned long long)(debug_ok ? debug_finished.dr[6] : 0),
                 (unsigned long long)finished.r_rax);
        if (info_ok && regs_ok && debug_ok && thread_info.pl_lwpid == thread && sig == SIGTRAP &&
            finished.r_rip == saved.r_rip && finished.r_rsp == (int64_t)(stack_address + 8) &&
            (debug_finished.dr[6] & 1)) {
            result = (int64_t)finished.r_rax;
            complete = 1;
            log_line("Shell bind: returned 0x%08X", (unsigned)(uint32_t)result);
        } else {
            log_line("Shell bind: unexpected stop/signal %d; restoring debugger state", sig);
        }
    } else {
        log_line("Shell bind: call did not reach return breakpoint (%d); requesting stop", waited);
    }

cleanup:
    if (attached && !stopped) {
        if (kill(pid, SIGSTOP) == 0) {
            forced_stop = 1;
            if (wait_stopped(pid, 1000, &sig) == 1) stopped = 1;
            else cleanup_ok = 0;
        } else cleanup_ok = 0;
    }
    if (attached && stopped) {
        if (debug_changed) {
            if (trace(PT_SETDBREGS, thread, &debug_saved, 0) != 0 ||
                trace(PT_GETDBREGS, thread, &debug_finished, 0) != 0 ||
                debug_finished.dr[0] != debug_saved.dr[0] || debug_finished.dr[7] != debug_saved.dr[7])
                cleanup_ok = 0;
        }
        if (stack_changed && !restore_memory(pid, PIOD_READ_D, PIOD_WRITE_D,
                                             stack_address, &original_stack, sizeof original_stack)) cleanup_ok = 0;
        if (regs_changed && saved_state) {
            if (trace(PT_SETFPREGS, thread, &floating, 0) != 0) cleanup_ok = 0;
            if (trace(PT_SETXSTATE, thread, extended, extended_info.xsave_len) != 0) cleanup_ok = 0;
            if (trace(PT_SETREGS, thread, &saved, 0) != 0) cleanup_ok = 0;
        }
        if (cleanup_ok && trace(PT_DETACH, pid, (void *)1, return_signal) != 0) cleanup_ok = 0;
        /* A late return trap can race our timeout SIGSTOP. After successful
         * restoration/detach, clear that requested stop so it cannot freeze
         * the shell immediately afterward. */
        if (cleanup_ok && forced_stop && kill(pid, SIGCONT) != 0) cleanup_ok = 0;
    }
    if (credentials_changed) {
        if (kernel_set_ucred_caps(me, caps) != 0) cleanup_ok = 0;
        if (kernel_set_ucred_authid(me, auth) != 0) cleanup_ok = 0;
    }
    if (!cleanup_ok) {
        log_line("Shell bind: restoration/detach failed; restart console before testing further");
        notify("PS5 XInput: debugger cleanup failed - restart console");
    } else {
        log_line("Shell bind: debugger state and payload credentials restored");
    }
    if (!complete && regs_changed)
        log_line("Shell bind: interrupted call may have service-side effects; success not confirmed");
    free(extended);
    return cleanup_ok && complete && (int32_t)result == 0;
}
