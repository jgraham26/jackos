#ifndef _INTTYPES_H
#define _INTTYPES_H 1

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Structure returned by imaxdiv */
typedef struct {
    intmax_t quot;
    intmax_t rem;
} imaxdiv_t;

/* --------------------------------------------------------------------------
 * Format specifiers for printf family functions (x86_64 LP64 model)
 * -------------------------------------------------------------------------- */

/* Signed decimal */
#define PRId8          "d"
#define PRId16         "d"
#define PRId32         "d"
#define PRId64         "ld"

#define PRIdLEAST8     "d"
#define PRIdLEAST16    "d"
#define PRIdLEAST32    "d"
#define PRIdLEAST64    "ld"

#define PRIdFAST8      "d"
#define PRIdFAST16     "ld"
#define PRIdFAST32     "ld"
#define PRIdFAST64     "ld"

#define PRIdMAX        "ld"
#define PRIdPTR        "ld"

/* Signed integer */
#define PRIi8          "i"
#define PRIi16         "i"
#define PRIi32         "i"
#define PRIi64         "li"

#define PRIiLEAST8     "i"
#define PRIiLEAST16    "i"
#define PRIiLEAST32    "i"
#define PRIiLEAST64    "li"

#define PRIiFAST8      "i"
#define PRIiFAST16     "li"
#define PRIiFAST32     "li"
#define PRIiFAST64     "li"

#define PRIiMAX        "li"
#define PRIiPTR        "li"

/* Unsigned octal */
#define PRIo8          "o"
#define PRIo16         "o"
#define PRIo32         "o"
#define PRIo64         "lo"

#define PRIoLEAST8     "o"
#define PRIoLEAST16    "o"
#define PRIoLEAST32    "o"
#define PRIoLEAST64    "lo"

#define PRIoFAST8      "o"
#define PRIoFAST16     "lo"
#define PRIoFAST32     "lo"
#define PRIoFAST64     "lo"

#define PRIoMAX        "lo"
#define PRIoPTR        "lo"

/* Unsigned decimal */
#define PRIu8          "u"
#define PRIu16         "u"
#define PRIu32         "u"
#define PRIu64         "lu"

#define PRIuLEAST8     "u"
#define PRIuLEAST16    "u"
#define PRIuLEAST32    "u"
#define PRIuLEAST64    "lu"

#define PRIuFAST8      "u"
#define PRIuFAST16     "lu"
#define PRIuFAST32     "lu"
#define PRIuFAST64     "lu"

#define PRIuMAX        "lu"
#define PRIuPTR        "lu"

/* Unsigned lowercase hexadecimal */
#define PRIx8          "x"
#define PRIx16         "x"
#define PRIx32         "x"
#define PRIx64         "lx"

#define PRIxLEAST8     "x"
#define PRIxLEAST16    "x"
#define PRIxLEAST32    "x"
#define PRIxLEAST64    "lx"

#define PRIxFAST8      "x"
#define PRIxFAST16     "lx"
#define PRIxFAST32     "lx"
#define PRIxFAST64     "lx"

#define PRIxMAX        "lx"
#define PRIxPTR        "lx"

/* Unsigned uppercase hexadecimal */
#define PRIX8          "X"
#define PRIX16         "X"
#define PRIX32         "X"
#define PRIX64         "lX"

#define PRIXLEAST8     "X"
#define PRIXLEAST16    "X"
#define PRIXLEAST32    "X"
#define PRIXLEAST64    "lX"

#define PRIXFAST8      "X"
#define PRIXFAST16     "lX"
#define PRIXFAST32     "lX"
#define PRIXFAST64     "lX"

#define PRIXMAX        "lX"
#define PRIXPTR        "lX"

/* --------------------------------------------------------------------------
 * Format specifiers for scanf family functions (x86_64 LP64 model)
 * -------------------------------------------------------------------------- */

#define SCNd8          "hhd"
#define SCNd16         "hd"
#define SCNd32         "d"
#define SCNd64         "ld"
#define SCNdFAST16     "ld"
#define SCNdFAST32     "ld"
#define SCNdFAST64     "ld"
#define SCNdMAX        "ld"
#define SCNdPTR        "ld"

#define SCNi8          "hhi"
#define SCNi16         "hi"
#define SCNi32         "i"
#define SCNi64         "li"
#define SCNiFAST16     "li"
#define SCNiFAST32     "li"
#define SCNiFAST64     "li"
#define SCNiMAX        "li"
#define SCNiPTR        "li"

#define SCNo8          "hho"
#define SCNo16         "ho"
#define SCNo32         "o"
#define SCNo64         "lo"
#define SCNoFAST16     "lo"
#define SCNoFAST32     "lo"
#define SCNoFAST64     "lo"
#define SCNoMAX        "lo"
#define SCNoPTR        "lo"

#define SCNu8          "hhu"
#define SCNu16         "hu"
#define SCNu32         "u"
#define SCNu64         "lu"
#define SCNuFAST16     "lu"
#define SCNuFAST32     "lu"
#define SCNuFAST64     "lu"
#define SCNuMAX        "lu"
#define SCNuPTR        "lu"

#define SCNx8          "hhx"
#define SCNx16         "hx"
#define SCNx32         "x"
#define SCNx64         "lx"
#define SCNxFAST16     "lx"
#define SCNxFAST32     "lx"
#define SCNxFAST64     "lx"
#define SCNxMAX        "lx"
#define SCNxPTR        "lx"

/* Standard function declarations */
intmax_t  imaxabs(intmax_t j);
imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom);
intmax_t  strtoimax(const char *nptr, char **endptr, int base);
uintmax_t strtoumax(const char *nptr, char **endptr, int base);

#ifdef __cplusplus
}
#endif

#endif /* _INTTYPES_H */