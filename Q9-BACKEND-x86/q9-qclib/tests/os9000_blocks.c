/* Execute QCC's packed edition-1 block builders against a mock trap. This
 * checks compiler output and wrapper contracts, not an OS-9000 kernel. */
#include <modes.h>
#include <sg_codes.h>
#include <errno.h>
#include <process.h>
#include <module.h>
#include <setsys.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <rbf.h>
#include <UNIX/stat.h>
extern int stat(const char *,struct stat *);
extern int chmod(const char *,int);
extern int chown(const char *,int);
extern int makdir(const char *,int,int,int);
extern void q9_x86_run_exit_handlers(void);
extern int q9_x86_exit_service(int);
struct q9_gblkmp_info { unsigned int minimum_block;unsigned int fragments;unsigned int total_ram;unsigned int total_free; };
extern int q9_gblkmp(void *,struct q9_gblkmp_info *);
int exit_order;
void exit_first(void) { exit_order=exit_order*10+1; }
void exit_second(void) { exit_order=exit_order*10+2; }
extern int q9_x86_path_open(const char *,const char *);
extern int q9_x86_path_seek(int,int,int);
extern int getstat(int,int,void *);
extern int remove(const char *);
extern int strlen(const char *);
extern void *attach(const char *,int);
extern int detach(void *);
extern int q9_x86_copy_memory(unsigned int,unsigned int,void *,void *);
char filename[16];
int fail_trap, bad_block, missing_file, truncated;
unsigned int position, wanted_mode, wanted_count;
unsigned int wanted_owner,wanted_group,wanted_mtime;

int q9_os9_trap_block(int *words)
{
    unsigned int *b = (unsigned int *)words;
    int code = b[0] & 65535;
    char *bytes = (char *)words;
    unsigned int *out;
    char *buffer;
    struct q9_os9000_process *process;
    struct mod_dir *modules;
    fd_stats *descriptor;
    if ((b[0] >> 16) != 1 || b[1] != 0) { bad_block = 1; return 199; }
    if (fail_trap) return fail_trap;
    if (code == 0x9f && missing_file) return 216;
    if (code == 0x9f || code == 0x99) {
        if (b[2] != (unsigned int)(code == 0x99 ? 30 : 22) || b[3] != (unsigned int)filename ||
            *(unsigned short *)(bytes + 16) != (wanted_mode | (code == 0x99 && missing_file ? 0x400 : 0))) { bad_block = 2; return 199; }
        if (code == 0x99 && *(unsigned int *)(bytes + 22) != 3) { bad_block = 3; return 199; }
        *(unsigned int *)(bytes + 18) = 0x12345678U;
    } else if (code == 0x98) {
        if (b[2] != 16 || b[3] != 0x12345678U) { bad_block = 4; return 199; }
    } else if (code == 0xa0 || code == 0xa4) {
        if (b[2] != 24 || b[3] != 0x12345678U || b[5] != wanted_count) { bad_block = 5; return 199; }
        if (code == 0xa0) { buffer = (char *)b[4]; buffer[0] = 'X'; buffer[1] = 'Y'; }
        b[5] = 2;
    } else if (code == 0xa2) {
        if (b[2] != 20 || b[3] != 0x12345678U) { bad_block = 6; return 199; }
        position = b[4];
    } else if (code == 0x9d) {
        if (b[2] != 22 || b[3] != 0x12345678U) { bad_block = 7; return 199; }
        out = (unsigned int *)*(unsigned int *)(bytes + 18);
        if (*(unsigned short *)(bytes + 16) == 5) *out = position;
        else if (*(unsigned short *)(bytes + 16) == 2) *out = 100;
        else if (*(unsigned short *)(bytes+16)==15) {
            if (out[0]!=48) { bad_block=26;return 199; }
            descriptor=(fd_stats *)out[1];descriptor->fd_flag=0x1234;descriptor->fd_owner=77;
            descriptor->fd_group=88;descriptor->fd_links=2;descriptor->fd_size=8192;
            descriptor->fd_atime=123456700U;descriptor->fd_utime=123456788U;
            descriptor->fd_mtime=123456789U;
        }
        else if (*(unsigned short *)(bytes+16)==0x7e) *out=0x12345;
        else { bad_block = 8; return 199; }
    } else if (code == 0xa3) {
        if (*(unsigned short *)(bytes+16)==28) {
            out=(unsigned int *)*(unsigned int *)(bytes+18);
            if (b[2]!=22 || b[3]!=0x12345678U || *out!=0x707) { bad_block=30;return 199; }
            return 0;
        }
        if (*(unsigned short *)(bytes+16)==15) {
            out=(unsigned int *)*(unsigned int *)(bytes+18);descriptor=(fd_stats *)*out;
            if (b[2]!=22 || b[3]!=0x12345678U || descriptor->fd_flag!=0x1234 || descriptor->fd_owner!=wanted_owner || descriptor->fd_group!=wanted_group || descriptor->fd_mtime!=wanted_mtime) { bad_block=27;return 199; }
            return 0;
        }
        if (b[2] != 22 || b[3] != 0x12345678U || *(unsigned short *)(bytes+16) != 2) { bad_block=15; return 199; }
        out=(unsigned int *)*(unsigned int *)(bytes+18);
        if (*out != 0) { bad_block=16; return 199; }
        truncated=1;
    } else if (code == 0x9a) {
        if (b[2] != 18 || b[3] != (unsigned int)filename || *(unsigned short *)(bytes + 16) != (wanted_mode | (code == 0x99 && missing_file ? 0x400 : 0))) { bad_block = 9; return 199; }
    } else if (code == 0x16) {
        if (b[2]!=24 || b[3]!=7 || b[5]!=236) { bad_block=11; return 199; }
        process=(struct q9_os9000_process *)b[4];
        process->p_id=7; process->p_pid=2; process->p_owner=0x00030004U;
        process->p_prior=91; process->p_datasz=4096; b[5]=236;
    } else if (code == 0x17) {
        if (b[2]!=20 || b[4]<12) { bad_block=12; return 199; }
        out=(unsigned int *)b[3]; out[0]=2;out[1]=0;out[2]=0x12340000U;b[4]=12;
    } else if (code == 0x15) {
        if (b[2]!=20 || b[4]<96) { bad_block=13; return 199; }
        modules=(struct mod_dir *)b[3];modules->md_mptr=(mh_com *)0x12340000U;
        modules->md_static=512;modules->md_link=3;b[4]=96;
    } else if (code == 0x93) {
        if (b[2]!=16) { bad_block=14; return 199; }
        b[3]=0xfedcba98U;
    } else if (code == 35) {
        if (b[2]!=20) { bad_block=24;return 199; }
        b[3]=1609459200U;b[4]=0x00640014U;
    } else if (code == 50) {
        if (b[2]!=16 || b[3]!=1609459200U) { bad_block=25;return 199; }
    } else if (code == 54) {
        if (b[2]!=20 || b[3]!=0x80000300U || b[4]!=0) { bad_block=23; return 199; }
        b[3]=7;b[4]=2;
    } else if (code == 150) {
        if (b[2]!=22 || b[3]!=(unsigned int)filename || *(unsigned short *)(bytes+16)!=3) { bad_block=21; return 199; }
        *(unsigned int *)(bytes+18)=0xfedc1234U;
    } else if (code == 155) {
        if (b[2]!=16 || b[3]!=0xfedc1234U) { bad_block=22; return 199; }
    } else if (code == 17) {
        if (b[2]!=16 || b[3]!=0x12345678U) { bad_block=20; return 199; }
    } else if (code == 158) {
        if (b[2]!=22 || b[3]!=(unsigned int)filename || *(unsigned short *)(bytes+16)!=2 || *(unsigned int *)(bytes+18)!=0x707) { bad_block=31;return 199; }
    } else if (code == 20) {
        if (b[2]!=40 || b[3]!=0 || b[5]!=1024) { bad_block=29;return 199; }
        out=(unsigned int *)b[4];out[0]=0x12340000U;out[1]=4096;
        b[6]=16;b[7]=1;b[8]=0x1000000U;b[9]=4096;
    } else if (code == 33) {
        if (b[2]!=34 || b[3]!=(unsigned int)filename || b[6]!=4 || *(unsigned short *)(bytes+28)!=0x101 || *(unsigned short *)(bytes+30)!=0x8000 || *(unsigned short *)(bytes+32)!=17) { bad_block=28;return 199; }
        b[4]=0x23456780U;b[5]=0x234567e0U;
        *(unsigned short *)(bytes+28)=0x201;*(unsigned short *)(bytes+30)=0x8001;
    } else if (code == 32) {
        if (b[2]!=28 || b[3]!=(unsigned int)filename || *(unsigned short *)(bytes+24)!=0x101 || *(unsigned short *)(bytes+26)!=0x8000) { bad_block=17; return 199; }
        b[3]+=1;b[4]=0x23456780U;b[5]=0x234567e0U;
        *(unsigned short *)(bytes+24)=0x201;*(unsigned short *)(bytes+26)=0x8001;
    } else if (code == 36) {
        if (b[2]!=16 || b[3]!=0x23456780U) { bad_block=18; return 199; }
    } else if (code == 9) {
        if (b[2]!=28 || b[3]!=0x12345U || b[6]!=2) { bad_block=19; return 199; }
        buffer=(char *)b[5];out=(unsigned int *)b[4];
        buffer[0]=*(char *)out;buffer[1]=*((char *)out+1);
    } else { bad_block = 10; return 199; }
    return 0;
}
int main(void)
{
    path_id path = 0;
    time_t stamp;unsigned int clock_ticks;
    char *name;mh_com *head;void *entry;unsigned short type,attr;
    unsigned int count, size;
    char buffer[8];
    unsigned int table[4];
    struct q9_os9000_process process;
    struct mod_dir modules[2];
    fd_stats descriptor;
    struct stat file_info;
    unsigned int block_map[256];struct q9_gblkmp_info map_info;
    if (sizeof(path_id) != 4) return 1;
    if (FAM_DIR!=0x8000 || S_IFDIR!=0x8000 || S_IOREAD!=0x100 || S_IOWRITE!=0x200 || S_IOEXEC!=0x400 || FAM_SIZE!=0x2000) return 47;
    filename[0] = 'x'; filename[1] = 0;
    wanted_mode = 3;
    if (_os_open(filename,3,&path) || path != 0x12345678U || bad_block) return 2;
    fail_trap = 207;path = 9;
    if (_os_open(filename,3,&path) != 207 || path != 9) return 3;
    fail_trap = 0;path = 0x12345678U;
    if (_os_close(path) || bad_block) return 4;
    count = 5;wanted_count = 5;
    if (_os_read(path,buffer,&count) || count != 2 || buffer[0] != 'X' || buffer[1] != 'Y') return 5;
    count = 4;wanted_count = 4;
    if (_os_write(path,buffer,&count) || count != 2) return 6;
    if (_os_seek(path,77) || position != 77) return 7;
    if (_os_gs_pos(path,&size) || size != 77) return 8;
    if (_os_gs_size(path,&size) || size != 100) return 9;
    wanted_mode = 0;
    if (_os_delete(filename,0) || remove(filename)) return 10;
    wanted_mode = 3;
    if (_os_create(filename,3,&path,3) || path != 0x12345678U) return 11;
    wanted_mode = 1;
    if (q9_x86_path_open(filename,"rb") != 0x12345678 || bad_block) return 12;
    position = 20;
    if (q9_x86_path_seek(path,-3,1) != 17 || position != 17) return 13;
    if (q9_x86_path_seek(path,-1,2) != 99 || position != 99) return 14;
    if (q9_x86_path_seek(path,-101,2) != -1) return 15;
    fail_trap = 208;
    if (getstat(path,5,&size) != -1 || errno != 208) return 16;
    fail_trap=0;
    if (sizeof process !=236 || sizeof(struct mod_dir)!=96) return 18;
    if ((char *)&process.p_prior-(char *)&process!=40) return 19;
    if ((char *)&modules[0].md_mptr-(char *)&modules[0]!=12) return 20;
    count=sizeof process;
    if (_os_gprdsc(7,&process,&count) || count!=236 || process.p_id!=7 || process.p_prior!=91 || process.p_datasz!=4096) return 21;
    count=sizeof table;
    if (_os_get_prtbl(table,&count) || count!=12 || table[0]!=2 || table[2]!=0x12340000U) return 22;
    count=sizeof modules;
    if (_os_get_moddir(modules,&count) || count!=96 || modules[0].md_static!=512 || modules[0].md_link!=3) return 23;
    if (q9_x86_device_list(&size) || size!=0xfedcba98U) return 24;
    wanted_mode=2;
    if (q9_x86_path_open(filename,"wb")!=0x12345678 || !truncated) return 25;
    missing_file=1;
    if (q9_x86_path_open(filename,"w")!=0x12345678) return 26;
    missing_file=0;fail_trap=214;
    if (q9_x86_path_open(filename,"w")!=-1 || errno!=214) return 27;
    fail_trap=0;
    if (q9_x86_path_open(filename,"r++")!=-1) return 28;
    name=filename;head=(mh_com *)0;entry=(void *)0;type=0x101;attr=0x8000;
    if (_os_link(&name,&head,&entry,&type,&attr) || name!=filename+1 || head!=(mh_com *)0x23456780U || entry!=(void *)0x234567e0U || type!=0x201 || attr!=0x8001) return 29;
    if (_os_unlink(head)) return 30;
    fail_trap=221;name=filename;type=0x101;attr=0x8000;
    if (_os_link(&name,&head,&entry,&type,&attr)!=221 || name!=filename || type!=0x101 || attr!=0x8000) return 31;
    fail_trap=0;filename[0]='A';filename[1]='B';
    if (q9_x86_copy_memory(0x12345,2,filename,buffer) || buffer[0]!='A' || buffer[1]!='B') return 32;
    if (atexit(exit_first) || atexit(exit_second)) return 33;
    q9_x86_run_exit_handlers();
    if (exit_order!=21) return 34;
    q9_x86_run_exit_handlers();
    if (exit_order!=21) return 35;
    if (q9_x86_exit_service(0x12345678)) return 36;
    if (attach(filename,3)!=(void *)0xfedc1234U || detach((void *)0xfedc1234U)) return 37;
    fail_trap=214;
    if (attach(filename,3)!=(void *)0 || errno!=214 || detach((void *)0xfedc1234U)!=-1) return 38;
    fail_trap=0;
    count=0x80000300U;
    if (_os9_sleep(&count) || count!=7) return 39;
    stamp=0;clock_ticks=0;
    if (_os_getime(&stamp,&clock_ticks) || stamp!=1609459200L || clock_ticks!=0x00640014U) return 40;
    if (time((time_t *)0)!=stamp || _os_setime(stamp)) return 41;
    fail_trap=222;stamp=77;clock_ticks=99;
    if (_os_getime(&stamp,&clock_ticks)!=222 || stamp!=77 || clock_ticks!=99) return 42;
    if (time(&stamp)!=-1 || errno!=222 || stamp!=77) return 43;
    fail_trap=0;
    if (sizeof descriptor!=48 || (char *)&descriptor.fd_mtime-(char *)&descriptor!=32) return 44;
    if (_os_gs_fd(path,sizeof descriptor,&descriptor) || descriptor.fd_flag!=0x1234 || descriptor.fd_owner!=77 || descriptor.fd_mtime!=123456789U) return 45;
    descriptor.fd_mtime=1609459200U;wanted_mtime=1609459200U;wanted_owner=77;wanted_group=88;
    if (_os_ss_fd(path,&descriptor)) return 46;
    type=0x101;attr=0x8000;
    if (_os_load(filename,&head,&entry,4,&type,&attr,17) || head!=(mh_com *)0x23456780U || entry!=(void *)0x234567e0U || type!=0x201 || attr!=0x8001) return 48;
    fail_trap=221;type=0x101;attr=0x8000;
    if (_os_load(filename,&head,&entry,4,&type,&attr,17)!=221 || type!=0x101 || attr!=0x8000) return 49;
    fail_trap=0;
    if (q9_gblkmp(block_map,&map_info) || map_info.minimum_block!=16 || map_info.fragments!=1 || map_info.total_ram!=0x1000000U || map_info.total_free!=4096 || block_map[0]!=0x12340000U || block_map[1]!=4096) return 50;
    wanted_mode=0;
    if (stat(filename,&file_info) || file_info.st_mode!=0x1234 || file_info.st_uid!=77 || file_info.st_gid!=88 || file_info.st_size!=8192 || file_info.st_ino!=0x12345 || file_info.st_nlink!=2 || file_info.st_mtime!=123456789L || file_info.st_ctime!=123456788L) return 51;
    if (chmod(filename,0x707)) return 52;
    if (makdir(filename,2,0x707,0)) return 53;
    wanted_mode=3;wanted_owner=0x5678;wanted_group=0x1234;wanted_mtime=123456789;
    if (chown(filename,0x12345678)) return 54;
    if (bad_block) return 17;
    return 0;
}
