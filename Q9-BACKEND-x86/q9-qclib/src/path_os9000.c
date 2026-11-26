/* Experimental OS-9000/x86 edition-1 path adapter.
 * Evidence: sibling Q9-OS-x86/kernel/c/kcall.c (open/read block builders),
 * ioman/c/rwseek.c (read/write/close), ergebnisse/dienstcodes.json.
 * Open uses the observed edition-1 packed output at +0x12, not the later
 * generated srvcb structure's aligned field at +0x14. Guest validation is
 * still required. Append mode is not yet supported. */
extern int q9_os9_trap_block(int *);

extern int _os_open(const char *, unsigned int, unsigned int *);
extern int _os_create(const char *, unsigned int, unsigned int *, unsigned int);
extern int _os_close(unsigned int);
extern int q9_x86_setstat(unsigned int, unsigned int, void *);
extern int errno;

int q9_x86_path_open(const char *name, const char *mode)
{
    unsigned int path, size = 0;
    int access, error, plus = 0, binary = 0;
    const char *p;
    if (!name || !mode || (mode[0] != 'r' && mode[0] != 'w')) return -1;
    p = mode + 1;
    while (*p) {
        if (*p == '+' && !plus) plus = 1;
        else if (*p == 'b' && !binary) binary = 1;
        else return -1;
        ++p;
    }
    access = plus ? 3 : (mode[0] == 'r' ? 1 : 2);
    error = _os_open(name, access, &path);
    if (mode[0] == 'w') {
        if (!error) {
            /* Preserve the directory entry and permissions. SS_Size's
             * argument is a pointer to a native 32-bit size (PCF path.c). */
            error = q9_x86_setstat(path, 2, &size);
            if (error) _os_close(path);
        } else if (error == 216) {
            /* E_PNNF only: a permission or device error must not trigger
             * create. NOCREATE protects a file created between the calls. */
            error = _os_create(name, access | 0x400, &path, 3);
        }
    }
    if (error) { errno = error; return -1; }
    return path;
}
int q9_x86_path_close(int path)
{
    unsigned int block[4];
    if (path < 0) return -1;
    block[0] = 0x00010098;
    block[1] = 0;
    block[2] = 0x10;
    block[3] = path;
    return q9_os9_trap_block((int *)block) == 0 ? 0 : -1;
}
static int path_transfer(int code, int path, char *buffer, int count)
{
    unsigned int block[6];
    int error;
    if (path < 0 || count < 0 || (!buffer && count)) return -1;
    block[0] = 0x00010000 | code;
    block[1] = 0;
    block[2] = 0x18;
    block[3] = path;
    block[4] = (unsigned int)buffer;
    block[5] = count;
    error = q9_os9_trap_block((int *)block);
    if (error == 211 && code == 0xa0) return 0;
    if (error || block[5] > (unsigned int)count) return -1;
    return block[5];
}
int q9_x86_path_read(int path, char *buffer, int count)
{
    return path_transfer(0xa0, path, buffer, count);
}
int q9_x86_path_write(int path, const char *buffer, int count)
{
    return path_transfer(0xa4, path, (char *)buffer, count);
}

/* GetStat's edition-1 pointer is packed at +18, as in kcall_getstat.
 * Seek's path/position fields are +12/+16 (i_seek_pb). */
int q9_x86_path_seek(int path, int offset, int origin)
{
    unsigned int block[6], position = 0;
    int i;
    if (path < 0 || origin < 0 || origin > 2) return -1;
    for (i = 0; i < 6; ++i) block[i] = 0;
    if (origin) {
        block[0] = 0x0001009d; block[2] = 0x16; block[3] = path;
        block[4] = origin == 1 ? 5 : 2;
        *(unsigned int *)((char *)block + 18) = (unsigned int)&position;
        if (q9_os9_trap_block((int *)block)) return -1;
    }
    if (position > 2147483647U) return -1;
    if (offset < 0) {
        if ((unsigned int)(0U - (unsigned int)offset) > position) return -1;
    } else if ((unsigned int)offset > 2147483647U - position) return -1;
    position += offset;
    block[0] = 0x000100a2; block[1] = 0; block[2] = 0x14;
    block[3] = path; block[4] = position;
    if (q9_os9_trap_block((int *)block)) return -1;
    return position;
}
