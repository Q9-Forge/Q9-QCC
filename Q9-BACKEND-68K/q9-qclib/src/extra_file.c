/*
 * File: extra_file.c
 * Description: Implement small stdio state and reopen helpers.
 * Owner: cla / afoe
 * Version: 0.2
 * Repository: Q9-Forge/Q9-QCC
 *
 * Edition History
 * 2026-11-26  0.2  Call the Q9 runtime symbols from the shared library.
 * 2026-09-11  0.1  Added stdio state helpers.
 */

/* Function: qf_clearerr
 * Clears the state of a stream.
 * Parameters: a Runtime argument frame containing the stream handle.
 * Returns: Zero.
 */
int qf_clearerr(int *a)
{
    return 0;
}

/* Function: qf_flush
 * Flushes pending stream output.
 * Parameters: a Runtime argument frame containing the stream handle.
 * Returns: Zero.
 */
int qf_flush(int *a)
{
    return 0;
}

extern char *tc_qf_open(int *a);
extern int tc_qf_close(int *a);

/* Function: qf_reopen
 * Reopens a stream for another file.
 * Parameters: a Runtime argument frame containing file name, mode and stream.
 * Returns: Stream handle.
 */
char *qf_reopen(int *a)
{
    int close_args[1];
    int open_args[2];
    close_args[0] = a[2];
    tc_qf_close(close_args);
    open_args[0] = a[0];
    open_args[1] = a[1];
    return tc_qf_open(open_args);
}
