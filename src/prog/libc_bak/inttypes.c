#include <inttypes.h>
#include <stdlib.h>

intmax_t imaxabs(intmax_t j) {
    return j < 0 ? -j : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t result;
    result.quot = numer / denom;
    result.rem = numer % denom;
    return result;
}

intmax_t strtoimax(const char *nptr, char **endptr, int base) {
    return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *nptr, char **endptr, int base) {
    return (uintmax_t)strtoull(nptr, endptr, base);
}