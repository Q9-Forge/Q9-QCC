/* Edition-1 F_TIME (35): seconds +12, packed ticks +16.
 * F_STIME (50): seconds +12. Epoch: OS-9 Technical Manual, F_TIME,
 * 1970-01-01 GMT. Block fields: Q9-OS-x86/kernel/c/svc_proc.c, clock.c. */
#include <time.h>
extern int q9_os9_trap_block(int *);
extern int errno;
int _os_getime(time_t *seconds,unsigned int *ticks)
{
    unsigned int block[5];int error;
    if (!seconds || !ticks) return -1;
    block[0]=0x10023;block[1]=0;block[2]=20;block[3]=0;block[4]=0;
    error=q9_os9_trap_block((int *)block);
    if (!error) { *seconds=block[3];*ticks=block[4]; }
    return error;
}
time_t time(time_t *output)
{
    time_t seconds;unsigned int ticks;int error;
    error=_os_getime(&seconds,&ticks);
    if (error) { errno=error;return -1; }
    if (output) *output=seconds;
    return seconds;
}
int _os_setime(unsigned int seconds)
{
    unsigned int block[4];
    block[0]=0x10032;block[1]=0;block[2]=16;block[3]=seconds;
    return q9_os9_trap_block((int *)block);
}
