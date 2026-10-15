/* Host-only stand-ins for FreeBSD interfaces, used to exercise the actual
 * cleanup code with controlled stops/failures. These do not validate PS5 ABI. */
#ifndef SHELL_BIND_SHIM_H
#define SHELL_BIND_SHIM_H
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdint.h>
#include <time.h>
#define CTL_KERN 1
#define KERN_PROC 14
#define KERN_PROC_PROC 8
#define COMMLEN 19
#define SYS_ptrace 101
#define PT_ATTACH 10
#define PT_DETACH 11
#define PT_IO 12
#define PT_LWPINFO 13
#define PT_GETREGS 33
#define PT_SETREGS 34
#define PT_GETFPREGS 35
#define PT_SETFPREGS 36
#define PT_GETDBREGS 37
#define PT_SETDBREGS 38
#define PT_CONTINUE 7
#define PT_GETXSTATE_INFO 68
#define PT_GETXSTATE 69
#define PT_SETXSTATE 70
#define PIOD_READ_D 1
#define PIOD_WRITE_D 2
#define PIOD_READ_I 3
#define PIOD_WRITE_I 4
struct kinfo_proc { int ki_structsize; pid_t ki_pid; char ki_comm[20]; };
struct ptrace_io_desc { int piod_op; void *piod_offs; void *piod_addr; size_t piod_len; };
struct reg { int64_t r_rsp, r_rip, r_rdi, r_rsi, r_rdx, r_rcx, r_r8, r_r9, r_rax; };
struct fpreg { unsigned char state[512]; };
struct dbreg { uint64_t dr[16]; };
struct ptrace_lwpinfo { pid_t pl_lwpid; };
struct ptrace_xstate_info { uint64_t xsave_mask; uint32_t xsave_len; };
int sysctl(const int *, unsigned, void *, size_t *, const void *, size_t);
long __syscall(long, ...);
uint64_t kernel_get_ucred_authid(pid_t);
int kernel_get_ucred_caps(pid_t, uint8_t *);
int kernel_set_ucred_authid(pid_t, uint64_t);
int kernel_set_ucred_caps(pid_t, const uint8_t *);
int kernel_dynlib_handle(pid_t, const char *, uint32_t *);
intptr_t kernel_dynlib_dlsym(pid_t, uint32_t, const char *);
intptr_t kernel_dynlib_resolve(pid_t, uint32_t, const char *);
int kernel_get_vmem_protection(pid_t, intptr_t, size_t);
int kernel_set_vmem_protection(pid_t, intptr_t, size_t, int);
void nid_encode(const char *, char *);
int mdbg_copyout(pid_t, intptr_t, void *, size_t);
int mdbg_copyin(pid_t, const void *, intptr_t, size_t);
pid_t shell_test_waitpid(pid_t, int *, int);
int shell_test_kill(pid_t, int);
int shell_test_clock(clockid_t, struct timespec *);
int shell_test_sleep(unsigned);
#define waitpid shell_test_waitpid
#define kill shell_test_kill
#define clock_gettime shell_test_clock
#define usleep shell_test_sleep
#endif
