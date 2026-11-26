/*--------------------------------------------------------------------------------------
 * File:        native_platform_test.c
 * Description: Check native command argument bounds and module cleanup.
 * Owner:       cla / afoe
 * Version:     0.02
 *
 * This file is part of the Q9-Forge repository. See https://github.com/Q9-Forge.
 *
 * Edition History
 * Date    | Ver. | Description                                               | By
 * 26-11-26| 0.01 | Add native command bounds regression checks.             | cla
 * 26-11-26| 0.02 | Test the maximum supported module name.                   | cla
 *--------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/platform_os9.c"
static int loads, unlinks, forks, legacy;
static char loaded_name[256];
/*--------------------------------------------------------------------------------------
 * Function:    _os_load
 * Description: Simulate loading a command module.
 * Parameter:   char *n: Module or file name.
 *              int m: Access mode.
 *              int c: Load options.
 *              void **p: Receives the module pointer.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_load(char *n, int m, int c, void **p)
{
    (void)m; (void)c;
    strcpy(loaded_name, n);
    ++loads;
    if (legacy && loads % 2) return -1;
    *p = (void *)1;
    return 0;
}
/*--------------------------------------------------------------------------------------
 * Function:    _os_unlink
 * Description: Record a module release.
 * Parameter:   void *p: Module pointer.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_unlink(void *p) { (void)p; ++unlinks; return 0; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_fork
 * Description: Record a command launch.
 * Parameter:   int t: Module type.
 *              long a: Additional memory size.
 *              long p: Parameter block size.
 *              int n: Number of inherited paths.
 *              int r: Process priority.
 *              char *s: Command name.
 *              char *v: Command parameter block.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_fork(int t, long a, long p, int n, int r, char *s, char *v)
{
    (void)t; (void)a; (void)p; (void)n; (void)r; (void)s; (void)v;
    ++forks;
    return 1;
}
/*--------------------------------------------------------------------------------------
 * Function:    _os_wait
 * Description: Return a successful child status.
 * Parameter:   int *s: Receives the child status.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_wait(int *s) { *s=0;return 1; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_open
 * Description: Simulate an unavailable file.
 * Parameter:   char *n: Module or file name.
 *              int m: Access mode.
 *              int *p: Receives the path number.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_open(char *n,int m,int *p) { (void)n;(void)m;(void)p;return -1; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_close
 * Description: Simulate closing a path.
 * Parameter:   int p: Path number.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_close(int p) { (void)p;return 0; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_gs_fd
 * Description: Simulate an unavailable file status.
 * Parameter:   int p: Path number.
 *              int n: Status buffer size.
 *              char *b: Status destination buffer.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_gs_fd(int p,int n,char *b) { (void)p;(void)n;(void)b;return -1; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_chgdir
 * Description: Simulate a successful directory change.
 * Parameter:   char *p: Destination directory.
 *              int m: Access mode.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
int _os_chgdir(char *p,int m) { (void)p;(void)m;return 0; }
/*--------------------------------------------------------------------------------------
 * Function:    _os_ticks
 * Description: Return a fixed clock value.
 * Parameter:   void: No parameters.
 * Return:      Simulated service result.
 *--------------------------------------------------------------------------------------*/
unsigned long _os_ticks(void) { return 0; }
/*--------------------------------------------------------------------------------------
 * Function:    main
 * Description: Exercise normal and oversized native commands.
 * Parameter:   void: No parameters.
 * Return:      Process exit status.
 *--------------------------------------------------------------------------------------*/
int main(void) {
 char huge[1100]; char max_name[243]; char *args[3]; int before;
 memset(huge,'x',sizeof(huge)-1); huge[sizeof(huge)-1]=0;
 args[0]="tool"; args[1]="ok"; args[2]=0;
 if(q9_exec_words(args,2)!=0 || forks!=1 || unlinks!=1) return 1;
 before=unlinks; args[1]=huge;
 if(q9_exec_words(args,2)!=-1 || unlinks!=before+1 || forks!=1) return 2;
 legacy=1; before=unlinks;
 if(q9_exec_words(args,2)!=-1 || unlinks!=before+1 || forks!=1) return 3;
 args[0]=huge; before=loads;
 if(q9_exec_words(args,1)!=-1 || loads!=before) return 4;
 if(qmake_run(huge)!=-1 || loads!=before) return 5;
 legacy=0;
 memset(max_name, 'n', sizeof(max_name)-1); max_name[sizeof(max_name)-1]=0;
 args[0]=max_name; args[1]=0;
 if(q9_exec_words(args,1)!=0 || strlen(loaded_name)!=255) return 6;
 puts("native bounds checks passed");return 0;
}
