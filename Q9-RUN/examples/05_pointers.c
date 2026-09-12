/* Pointers: address operator, dereference, pointer arithmetic, and pointers to
   Struct. Zeigt: ADDRL/ADDRG/PUSHADDR, LOADIND/STOREIND, PADD/PDIFF
   (Zeigerarithmetik ist eigenstaendig, keine gewoehnliche ADD/SUB). */
struct Node {
    int value;
};

int add_via_pointer(int a, int b) {
    int *pa, *pb;
    pa = &a;
    pb = &b;
    return *pa + *pb;
}

int bump_via_struct_pointer(struct Node *n) {
    n->value = n->value + 1;
    return n->value;
}

int array_walk(int *base, int count) {
    int *p, *end, total;
    total = 0;
    p = base;
    end = base + count;
    while (p != end) {
        total = total + *p;
        p = p + 1;
    }
    return total;
}

int numbers[3];

int main(void) {
    struct Node n;
    n.value = 41;
    putint(add_via_pointer(4, 5));       /* erwartet: 9 */
    putint(bump_via_struct_pointer(&n)); /* erwartet: 42 */
    numbers[0] = 1; numbers[1] = 2; numbers[2] = 3;
    putint(array_walk(numbers, 3));      /* erwartet: 6 */
    return 0;
}
