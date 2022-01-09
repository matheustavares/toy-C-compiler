#ifndef _UTIL_H
#define _UTIL_H

#include <assert.h>
#include <unistd.h>

#include "lib/error.h"
#include "lib/wrappers.h"
#include "lib/string-util.h"

/* CAREFUL: evaluates a and b twice! */
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#endif
