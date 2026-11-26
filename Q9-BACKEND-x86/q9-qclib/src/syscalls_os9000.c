#include <module.h>
/* Experimental edition-1 OS-9000/x86 service wrappers. These use the packed
 * layouts observed in Q9-OS-x86/kernel/c/kcall.c, ioman/c/stat.c, svcio.c and
 * pcf/c/open.c. They are not a claim of validation on an OS-9000 guest.
 * The four-argument create form is supported; initial-size varargs are not. */
extern int q9_os9_trap_block(int *);
int errno;

static void service_block(unsigned int *block, int words, int code, int size)
{
    int i;
    for (i = 0; i < words; ++i) block[i] = 0;
    block[0] = 0x10000U | code;
    block[2] = size;
}
int _os_open(const char *name, unsigned int mode, unsigned int *path)
{
    unsigned int block[6]; int error;
    if (!name || !path || mode > 65535U) return -1;
    service_block(block, 6, 0x9f, 0x16);
    block[3] = (unsigned int)name;
    *(unsigned short *)((char *)block + 16) = mode;
    error = q9_os9_trap_block((int *)block);
    if (!error) *path = *(unsigned int *)((char *)block + 18);
    return error;
}
int _os_close(unsigned int path)
{
    unsigned int block[4]; service_block(block, 4, 0x98, 0x10);
    block[3] = path; return q9_os9_trap_block((int *)block);
}
static int service_transfer(int code, unsigned int path, void *buffer, unsigned int *count)
{
    unsigned int block[6]; int error;
    if (!count || (!buffer && *count)) return -1;
    service_block(block, 6, code, 0x18);
    block[3] = path; block[4] = (unsigned int)buffer; block[5] = *count;
    error = q9_os9_trap_block((int *)block);
    *count = block[5]; return error;
}
int _os_read(unsigned int path, void *buffer, unsigned int *count)
{ return service_transfer(0xa0, path, buffer, count); }
int _os_write(unsigned int path, void *buffer, unsigned int *count)
{ return service_transfer(0xa4, path, buffer, count); }
int _os_seek(unsigned int path, unsigned int position)
{
    unsigned int block[5]; service_block(block, 5, 0xa2, 0x14);
    block[3] = path; block[4] = position;
    return q9_os9_trap_block((int *)block);
}
int _os_delete(const char *name, unsigned int mode)
{
    unsigned int block[5];
    if (!name || mode > 65535U) return -1;
    service_block(block, 5, 0x9a, 0x12);
    block[3] = (unsigned int)name;
    *(unsigned short *)((char *)block + 16) = mode;
    return q9_os9_trap_block((int *)block);
}
int _os_create(const char *name, unsigned int mode, unsigned int *path, unsigned int permissions)
{
    unsigned int block[8]; int error;
    if (!name || !path || mode > 65535U || (mode & 0x2000U)) return -1;
    service_block(block, 8, 0x99, 0x1e);
    block[3] = (unsigned int)name;
    *(unsigned short *)((char *)block + 16) = mode;
    *(unsigned int *)((char *)block + 22) = permissions;
    error = q9_os9_trap_block((int *)block);
    if (!error) *path = *(unsigned int *)((char *)block + 18);
    return error;
}
static int service_status(int code, unsigned int path, unsigned int status, void *buffer)
{
    unsigned int block[6];
    if (status > 65535U || !buffer) return -1;
    service_block(block, 6, code, 0x16);
    block[3] = path;
    *(unsigned short *)((char *)block + 16) = status;
    *(unsigned int *)((char *)block + 18) = (unsigned int)buffer;
    return q9_os9_trap_block((int *)block);
}
int q9_x86_getstat(unsigned int path, unsigned int status, void *buffer)
{ return service_status(0x9d, path, status, buffer); }
int q9_x86_setstat(unsigned int path, unsigned int status, void *buffer)
{ return service_status(0xa3, path, status, buffer); }
int getstat(int path, int status, void *buffer)
{
    int error = q9_x86_getstat(path, status, buffer);
    if (error) { errno = error; return -1; } return 0;
}
int setstat(int path, int status, void *buffer)
{
    int error = q9_x86_setstat(path, status, buffer);
    if (error) { errno = error; return -1; } return 0;
}
int _os_gs_pos(unsigned int path, unsigned int *position)
{ return q9_x86_getstat(path, 5, position); }
int _os_gs_size(unsigned int path, unsigned int *size)
{ return q9_x86_getstat(path, 2, size); }
int _os_gs_devnm(unsigned int path, char *name)
{ return q9_x86_getstat(path, 14, name); }
int _gs_devn(int path, char *name)
{ return getstat(path, 14, name); }
int unlinkx(const char *name, int mode)
{
    int error = _os_delete(name, mode);
    if (error) { errno = error; return -1; } return 0;
}
int remove(const char *name) { return unlinkx(name, 0); }
int unlink(const char *name) { return unlinkx(name, 0); }

int _os_get_moddir(void *buffer, unsigned int *count)
{
    unsigned int block[5]; int error;
    if (!buffer || !count) return -1;
    service_block(block, 5, 0x15, 0x14);
    block[3] = (unsigned int)buffer; block[4] = *count;
    error = q9_os9_trap_block((int *)block);
    *count = block[4]; return error;
}

int _os_get_prtbl(void *buffer, unsigned int *count)
{
    unsigned int block[5]; int error;
    if (!buffer || !count) return -1;
    service_block(block,5,0x17,0x14);
    block[3]=(unsigned int)buffer; block[4]=*count;
    error=q9_os9_trap_block((int *)block); *count=block[4]; return error;
}
int _os_gprdsc(unsigned int pid, void *buffer, unsigned int *count)
{
    unsigned int block[6]; int error;
    if (!buffer || !count) return -1;
    service_block(block,6,0x16,0x18);
    block[3]=pid; block[4]=(unsigned int)buffer; block[5]=*count;
    error=q9_os9_trap_block((int *)block); *count=block[5]; return error;
}
int q9_x86_device_list(unsigned int *address)
{
    unsigned int block[4]; int error;
    if (!address) return -1;
    service_block(block,4,0x93,0x10);
    error=q9_os9_trap_block((int *)block);
    if (!error) *address=block[3]; return error;
}

/* F$Link (32), F$UnLink (36) and F$CpyMem (9): edition-1 fields
 * from Q9-OS-x86/inc/pb.h and kernel/c/svc_misc.c. In/out values live
 * inside the block; these fields are not pointers to caller variables. */
int _os_link(char **name, mh_com **head, void **entry,
             unsigned short *type, unsigned short *attr)
{
    unsigned int block[7]; int error;
    if (!name || !*name || !head || !entry || !type || !attr) return -1;
    service_block(block,7,32,28);
    block[3]=(unsigned int)*name;
    *(unsigned short *)((char *)block+24)=*type;
    *(unsigned short *)((char *)block+26)=*attr;
    error=q9_os9_trap_block((int *)block);
    if (!error) {
        *name=(char *)block[3]; *head=(mh_com *)block[4]; *entry=(void *)block[5];
        *type=*(unsigned short *)((char *)block+24);
        *attr=*(unsigned short *)((char *)block+26);
    }
    return error;
}
int _os_unlink(mh_com *head)
{
    unsigned int block[4];
    if (!head) return -1;
    service_block(block,4,36,16); block[3]=(unsigned int)head;
    return q9_os9_trap_block((int *)block);
}
int q9_x86_copy_memory(unsigned int pid, unsigned int count, void *from, void *to)
{
    unsigned int block[7];
    if (count && (!from || !to)) return -1;
    service_block(block,7,9,28);
    block[3]=pid; block[4]=(unsigned int)from; block[5]=(unsigned int)to; block[6]=count;
    return q9_os9_trap_block((int *)block);
}

/* I$Attach edition 1 writes the device address at packed offset +18,
 * as in ioman/c/svcio.c; the later aligned SDK field at +20 differs. */
void *attach(const char *name, int mode)
{
    unsigned int block[6]; int error;
    if (!name || mode<0 || mode>65535) return (void *)0;
    service_block(block,6,150,22); block[3]=(unsigned int)name;
    *(unsigned short *)((char *)block+16)=mode;
    error=q9_os9_trap_block((int *)block);
    if (error) { errno=error; return (void *)0; }
    return (void *)*(unsigned int *)((char *)block+18);
}
int detach(void *device)
{
    unsigned int block[4]; int error;
    if (!device) return -1;
    service_block(block,4,155,16); block[3]=(unsigned int)device;
    error=q9_os9_trap_block((int *)block);
    if (error) { errno=error; return -1; }
    return 0;
}

/* F$Sleep (54): ticks and wake signal at +12/+16. The kernel handles
 * the high-bit 1/256-second form; no assumed system tick rate is needed. */
int _os9_sleep(unsigned int *ticks)
{
    unsigned int block[5]; int error;
    if (!ticks) return -1;
    service_block(block,5,54,20); block[3]=*ticks;
    error=q9_os9_trap_block((int *)block); *ticks=block[3];
    return error;
}

/* F$Load (33): name/head/entry +12/+16/+20, mode +24,
 * type/attributes +28/+30, 16-bit color +32; svc_gen.c svc_f_load.
 * Edition-1 packed size covers the final color field (34 bytes). */
int _os_load(const char *name,mh_com **head,void **entry,unsigned int mode,
             unsigned short *type,unsigned short *attr,unsigned int color)
{
    unsigned int block[9];int error;
    if (!name || !head || !entry || !type || !attr || mode>65535U || color>65535U) return -1;
    service_block(block,9,33,34);block[3]=(unsigned int)name;block[6]=mode;
    *(unsigned short *)((char *)block+28)=*type;
    *(unsigned short *)((char *)block+30)=*attr;
    *(unsigned short *)((char *)block+32)=color;
    error=q9_os9_trap_block((int *)block);
    if (!error) {
        *head=(mh_com *)block[4];*entry=(void *)block[5];
        *type=*(unsigned short *)((char *)block+28);
        *attr=*(unsigned short *)((char *)block+30);
    }
    return error;
}

struct q9_gblkmp_info {
    unsigned int minimum_block;
    unsigned int fragments;
    unsigned int total_ram;
    unsigned int total_free;
};
/* Q9 mfree adapter: map must hold 256 words (128 address/size pairs).
 * F$GBlkMp (20), fields +12..+36: svc_misc.c svc_f_gblkmp. */
int q9_gblkmp(void *map,struct q9_gblkmp_info *info)
{
    unsigned int block[10];int error;
    if (!map || !info) return -1;
    service_block(block,10,20,40);block[4]=(unsigned int)map;block[5]=1024;
    error=q9_os9_trap_block((int *)block);
    if (!error) {
        info->minimum_block=block[6];info->fragments=block[7];
        info->total_ram=block[8];info->total_free=block[9];
    }
    return error;
}
