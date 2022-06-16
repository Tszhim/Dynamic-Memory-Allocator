#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    /*
    double* ptr = sf_malloc(sizeof(double));

    *ptr = 320320320e-320;

    printf("%f\n", *ptr);

    sf_free(ptr);
    */

    /*
    sf_malloc(50);
    void* x = sf_malloc(200);
    void* z = sf_malloc(200);
    void *y = sf_malloc(200);
    sf_malloc(50);

    sf_show_heap();

    sf_free(x);
    sf_free(y);
    sf_show_heap();

    sf_free(z);
    sf_show_heap();
    */

    /*
    sf_set_magic(0x0);

    size_t sz_x = sizeof(double) * 8, sz_y = sizeof(int);
    void *x = sf_malloc(sz_x);
    sf_show_heap();

    void *y = sf_realloc(x, sz_y);

    sf_show_heap();
    y = y;
    */

    /*
    sf_set_magic(0x0);

    void *a = sf_malloc(60);
    void *b = sf_malloc(60);
    void *c = sf_malloc(60);
    void *d = sf_malloc(60);
    void *e = sf_malloc(60);
    void *f = sf_malloc(60);

    sf_free(a);
    sf_free(b);
    sf_free(c);
    sf_free(d);
    sf_free(e);
    sf_show_heap();
    sf_free(f);


    sf_show_heap();
    */

    sf_malloc(2000);
    sf_malloc(10);
    sf_show_heap();
    printf("%f", sf_peak_utilization());

    return EXIT_SUCCESS;
}
