/* Structs: single variable, array of structs, and field access.
   Shows field offsets (PUSH <offset> + IPADD/PADD), LOADIND/STOREIND,
   and the difference between one struct variable (address in the slot) and
   an element of a struct array (address = base + i*size). */
struct Point {
    int x;
    int y;
};

struct Point points[4];

int move_point(int i, int dx, int dy) {
    struct Point p;
    p = points[i];
    p.x = p.x + dx;
    p.y = p.y + dy;
    points[i] = p;
    return points[i].x + points[i].y;
}

int main(void) {
    points[0].x = 10;
    points[0].y = 20;
    putint(move_point(0, 1, 2)); /* erwartet: 11 + 22 = 33 */
    return 0;
}
