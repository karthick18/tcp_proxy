#ifndef _LOG_H_
#define _LOG_H_

#include <stdarg.h>

#define log(fmt, arg...) do { printf(fmt, ##arg); } while(0)
#define log_error(fmt, arg...) do { fprintf(stderr, fmt, ##arg); } while(0)
#define log_debug(verbose, fmt, arg...) do {    \
    if( (verbose) > 0) {                        \
        printf(fmt, ##arg);                     \
    }                                           \
}while(0)

#endif
