/* ===-- muldc3.c - Implement __muldc3 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is dual licensed under the MIT and the University of Illinois Open
 * Source Licenses. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __muldc3 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "libm.h"

#define CRT_INFINITY __builtin_huge_valf()

#define crt_isinf(x) __builtin_isinf((x))
#define crt_isnan(x) __builtin_isnan((x))
#define crt_copysign(x, y) __builtin_copysign((x), (y))
#define crt_copysignf(x, y) __builtin_copysignf((x), (y))
#define crt_copysignl(x, y) __builtin_copysignl((x), (y))

/* Returns: the product of a + ib and c + id */

double _Complex
__muldc3(double __a, double __b, double __c, double __d)
{
    double __ac = __a * __c;
    double __bd = __b * __d;
    double __ad = __a * __d;
    double __bc = __b * __c;
    double _Complex z;
    __real__ z = __ac - __bd;
    __imag__ z = __ad + __bc;
    if (crt_isnan(__real__ z) && crt_isnan(__imag__ z))
    {
        int __recalc = 0;
        if (crt_isinf(__a) || crt_isinf(__b))
        {
            __a = crt_copysign(crt_isinf(__a) ? 1 : 0, __a);
            __b = crt_copysign(crt_isinf(__b) ? 1 : 0, __b);
            if (crt_isnan(__c))
                __c = crt_copysign(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysign(0, __d);
            __recalc = 1;
        }
        if (crt_isinf(__c) || crt_isinf(__d))
        {
            __c = crt_copysign(crt_isinf(__c) ? 1 : 0, __c);
            __d = crt_copysign(crt_isinf(__d) ? 1 : 0, __d);
            if (crt_isnan(__a))
                __a = crt_copysign(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysign(0, __b);
            __recalc = 1;
        }
        if (!__recalc && (crt_isinf(__ac) || crt_isinf(__bd) ||
                          crt_isinf(__ad) || crt_isinf(__bc)))
        {
            if (crt_isnan(__a))
                __a = crt_copysign(0, __a);
            if (crt_isnan(__b))
                __b = crt_copysign(0, __b);
            if (crt_isnan(__c))
                __c = crt_copysign(0, __c);
            if (crt_isnan(__d))
                __d = crt_copysign(0, __d);
            __recalc = 1;
        }
        if (__recalc)
        {
            __real__ z = CRT_INFINITY * (__a * __c - __b * __d);
            __imag__ z = CRT_INFINITY * (__a * __d + __b * __c);
        }
    }
    return z;
}
