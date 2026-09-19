/* Small stdio state helpers. qclib file I/O is unbuffered for writes. */
int qf_clearerr(int *a)
{
    return 0;
}

int qf_flush(int *a)
{
    return 0;
}
