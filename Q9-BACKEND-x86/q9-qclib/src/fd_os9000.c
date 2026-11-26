/* Edition-1 descriptor status arguments. GetStat SS_FD receives
 * {size, buffer}; SetStat SS_FD receives {buffer}. Evidence: PCF stat.c
 * getstat_file_description, nav.c setstat_set_times; public 48-byte prefix
 * from the OS-9000 SDK rbf.h. No 68k descriptor layout is reused. */
#include <rbf.h>
extern int q9_x86_getstat(unsigned int,unsigned int,void *);
extern int q9_x86_setstat(unsigned int,unsigned int,void *);
int _os_gs_fd(unsigned int path,unsigned int size,fd_stats *buffer)
{
    unsigned int parameters[2];
    if (!buffer || size<sizeof(fd_stats)) return -1;
    parameters[0]=size;parameters[1]=(unsigned int)buffer;
    return q9_x86_getstat(path,15,parameters);
}
int _os_ss_fd(unsigned int path,fd_stats *buffer)
{
    unsigned int pointer;
    if (!buffer) return -1;
    pointer=(unsigned int)buffer;
    return q9_x86_setstat(path,15,&pointer);
}

#include <UNIX/stat.h>
extern int errno;
int _os_gs_fdaddr(unsigned int path,unsigned int *address)
{ return q9_x86_getstat(path,0x7e,address); }
int fstat(int path,struct stat *info)
{
    fd_stats descriptor;unsigned int address;int error;
    if (!info) return -1;
    error=_os_gs_fd(path,sizeof descriptor,&descriptor);
    if (!error) error=_os_gs_fdaddr(path,&address);
    if (error) { errno=error;return -1; }
    info->st_mode=descriptor.fd_flag;info->st_nlink=descriptor.fd_links;
    info->st_uid=descriptor.fd_owner;info->st_gid=descriptor.fd_group;
    info->st_size=descriptor.fd_size;info->st_atime=descriptor.fd_atime;
    info->st_mtime=descriptor.fd_mtime;info->st_ctime=descriptor.fd_utime;
    info->st_ino=address;
    /* st_dev/st_rdev are undefined by the OS-9 library contract. */
    info->st_dev=0;info->st_rdev=0;
    return 0;
}
static int metadata_open(const char *name,unsigned int *path)
{
    int error;
    if (!name || !path) return -1;
    error=_os_open(name,0,path);
    if (error==214) error=_os_open(name,FAM_DIR,path);
    return error;
}
int stat(const char *name,struct stat *info)
{
    unsigned int path;int error,result;
    if (!info) return -1;
    error=metadata_open(name,&path);
    if (error) { errno=error;return -1; }
    result=fstat(path,info);error=_os_close(path);
    if (!result && error) { errno=error;return -1; }
    return result;
}
int chmod(const char *name,int permissions)
{
    unsigned int path,attributes;int error,close_error;
    error=metadata_open(name,&path);
    if (error) { errno=error;return -1; }
    attributes=(unsigned short)permissions;
    error=q9_x86_setstat(path,28,&attributes);
    close_error=_os_close(path);
    if (!error) error=close_error;
    if (error) { errno=error;return -1; }
    return 0;
}

extern int q9_os9_trap_block(int *);
int q9_x86_makdir(const char *name,unsigned int mode,unsigned int permissions)
{
    unsigned int block[6];int i;
    if (!name || mode>65535U || (mode&FAM_SIZE)) return -1;
    for (i=0;i<6;++i) block[i]=0;
    block[0]=0x1009e;block[2]=22;block[3]=(unsigned int)name;
    *(unsigned short *)((char *)block+16)=mode;
    *(unsigned int *)((char *)block+18)=permissions;
    return q9_os9_trap_block((int *)block);
}
int makdir(const char *name,int mode,int permissions,int initial_size)
{
    int error;
    if (initial_size || (mode&FAM_SIZE)) return -1;
    error=q9_x86_makdir(name,(unsigned short)mode,(unsigned short)permissions);
    if (error) { errno=error;return -1; }
    return 0;
}

/* Same read/modify/write policy as 68k extra_chown.c; OS-9000 stores
 * 16-bit group and owner fields instead of two legacy owner bytes. */
int chown(const char *name,int owner)
{
    unsigned int path;fd_stats descriptor;int error,close_error;
    if (!name) return -1;
    error=_os_open(name,FAM_READ|FAM_WRITE,&path);
    if (error==214) error=_os_open(name,FAM_READ|FAM_WRITE|FAM_DIR,&path);
    if (error) { errno=error;return -1; }
    error=_os_gs_fd(path,sizeof descriptor,&descriptor);
    if (!error) {
        descriptor.fd_group=(unsigned int)owner>>16;
        descriptor.fd_owner=(unsigned int)owner&65535U;
        error=_os_ss_fd(path,&descriptor);
    }
    close_error=_os_close(path);if (!error) error=close_error;
    if (error) { errno=error;return -1; }
    return 0;
}
