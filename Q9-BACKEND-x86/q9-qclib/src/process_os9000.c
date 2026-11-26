/* Experimental edition-1 termination adapter. F$Exit (17), block size 16,
 * status at +12: Q9-OS-x86/inc/pb.h and kernel/c/svc_misc.c. */
extern int q9_os9_trap_block(int *);
extern void q9_x86_close_all_streams(void);
typedef void (*q9_exit_handler)(void);
q9_exit_handler q9_exit_handlers[32];
int q9_exit_handler_count;

int atexit(q9_exit_handler handler)
{
    if (!handler || q9_exit_handler_count == 32) return -1;
    q9_exit_handlers[q9_exit_handler_count++] = handler;
    return 0;
}
/* Separate from F$Exit so callback order can be checked with a mock trap. */
void q9_x86_run_exit_handlers(void)
{
    q9_exit_handler handler;
    while (q9_exit_handler_count) {
        --q9_exit_handler_count;
        handler=q9_exit_handlers[q9_exit_handler_count];
        handler();
    }
}
int q9_x86_exit_service(int status)
{
    unsigned int block[4];
    block[0]=0x10011;block[1]=0;block[2]=16;block[3]=status;
    return q9_os9_trap_block((int *)block);
}
void _Exit(int status)
{
    q9_x86_exit_service(status);
    /* A successful termination never returns. Do not resume caller code
     * if the experimental trap unexpectedly returns. */
    for (;;) { }
}
void exit(int status)
{
    q9_x86_run_exit_handlers();
    q9_x86_close_all_streams();
    _Exit(status);
}
