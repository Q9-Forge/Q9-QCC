/* Small stdio state helpers. qclib file I/O is unbuffered for writes. */
int qf_clearerr(int *a)
{
    return 0;
}

int qf_flush(int *a)
{
    return 0;
}

extern char *qf_open(int *a);
extern int qf_close(int *a);

char *qf_reopen(int *a)
{
    int close_args[1];
    int open_args[2];
    close_args[0] = a[2];
    qf_close(close_args);
    open_args[0] = a[0];
    open_args[1] = a[1];
    return qf_open(open_args);
}
