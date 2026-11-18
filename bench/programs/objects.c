#include <stdio.h>
#include <stdlib.h>
/* Objects the way C does them: a table of methods, and each object on the
   heap (Cabby objects live on the heap too). */
struct shape;
struct shape_vt { long (*area)(const struct shape *); };
struct shape { const struct shape_vt *vt; int size; };
static long square_area(const struct shape *s) { return (long)s->size * s->size; }
static long triangle_area(const struct shape *s) { return (long)s->size * s->size / 2; }
static const struct shape_vt square_vt = { square_area }, triangle_vt = { triangle_area };
__attribute__((noinline)) static long measure(const struct shape *s) { return s->vt->area(s); }
int main(void) {
    int n = @N@;
    long total = 0;
    for (int i = 0; i < n; i++) {
        struct shape *s = malloc(sizeof *s);
        s->vt = i % 2 == 0 ? &square_vt : &triangle_vt;
        s->size = i % 100;
        total += measure(s);
        free(s);
    }
    printf("%ld\n", total);
    return 0;
}
