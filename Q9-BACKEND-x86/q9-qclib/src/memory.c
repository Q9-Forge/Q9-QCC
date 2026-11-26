/* Process-local, four-byte-aligned arena allocator. Headers contain capacity
 * and an in-use flag. Free blocks are split and adjacent blocks coalesced.
 * No OS-9000 kernel allocation ABI is assumed here. */
typedef unsigned int size_t;
#define Q9_HEAP_BYTES 65536U
unsigned int q9_heap_words[16384];
unsigned int q9_heap_used;

void *malloc(size_t bytes)
{
    unsigned int capacity, offset = 0, old;
    unsigned char *heap = (unsigned char *)q9_heap_words;
    unsigned int *header;
    if (!bytes || bytes > Q9_HEAP_BYTES - 8U) return (void *)0;
    capacity = (bytes + 3U) & ~3U;
    while (offset < q9_heap_used) {
        header = (unsigned int *)(heap + offset);
        old = header[0];
        if (!header[1] && old >= capacity) {
            if (old >= capacity + 12U) {
                header[0] = capacity;
                header = (unsigned int *)(heap + offset + 8U + capacity);
                header[0] = old - capacity - 8U;
                header[1] = 0;
                header = (unsigned int *)(heap + offset);
            }
            header[1] = 1;
            return (void *)(heap + offset + 8U);
        }
        offset += old + 8U;
    }
    if (capacity + 8U > Q9_HEAP_BYTES - q9_heap_used) return (void *)0;
    header = (unsigned int *)(heap + q9_heap_used);
    header[0] = capacity; header[1] = 1;
    q9_heap_used += capacity + 8U;
    return (void *)((unsigned char *)header + 8);
}

void free(void *pointer)
{
    unsigned int offset = 0, next;
    unsigned char *heap = (unsigned char *)q9_heap_words;
    unsigned int *header, *following;
    if (!pointer) return;
    while (offset < q9_heap_used) {
        header = (unsigned int *)(heap + offset);
        if ((void *)(heap + offset + 8U) == pointer) { header[1] = 0; break; }
        offset += header[0] + 8U;
    }
    offset = 0;
    while (offset < q9_heap_used) {
        header = (unsigned int *)(heap + offset);
        next = offset + header[0] + 8U;
        if (!header[1]) {
            if (next == q9_heap_used) { q9_heap_used = offset; break; }
            following = (unsigned int *)(heap + next);
            if (!following[1]) { header[0] += following[0] + 8U; continue; }
        }
        offset = next;
    }
}

void *calloc(size_t count, size_t bytes)
{
    unsigned int total, i;
    unsigned char *p;
    if (bytes && count > (Q9_HEAP_BYTES - 8U) / bytes) return (void *)0;
    total = count * bytes;
    p = (unsigned char *)malloc(total);
    if (!p) return (void *)0;
    for (i = 0; i < total; ++i) p[i] = 0;
    return (void *)p;
}

void *realloc(void *pointer, size_t bytes)
{
    unsigned char *old = (unsigned char *)pointer, *fresh;
    unsigned int capacity, i;
    if (!old) return malloc(bytes);
    if (!bytes) { free(pointer); return (void *)0; }
    capacity = *(unsigned int *)(old - 8);
    if (bytes <= capacity) return pointer;
    fresh = (unsigned char *)malloc(bytes);
    if (!fresh) return (void *)0;
    for (i = 0; i < capacity; ++i) fresh[i] = old[i];
    free(pointer);
    return (void *)fresh;
}
