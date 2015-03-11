/*
 *  linux/lib/vsprintf.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/* vsprintf.c -- Lars Wirzenius & Linus Torvalds. */
/*
 * Wirzenius wrote this portably, Torvalds fucked it up :-)
 */

/*
 * Fri Jul 13 2001 Crutcher Dunnavant <crutcher+kernel@datastacks.com>
 * - changed to provide snprintf and vsnprintf functions
 * So Feb  1 16:51:32 CET 2004 Juergen Quade <quade@hsnr.de>
 * - scnprintf and vscnprintf
 */

/*
 * Nov 12, 2014 Ben Niu <niuben003@gmail.com>
 * - removed redundant sscan-family functions
 * Linux kernel version 3.4.104 -- lib/vsprintf.c 
 */
#include <def.h>
#include <stdarg.h>
#include <string.h>
#include <mm.h>
#include <io.h>

/*
 *  Determine whether some value is a power of two, where zero is
 * *not* considered a power of two.
 */
static inline __attribute__((const))
bool is_power_of_2(unsigned long n)
{
  return (n != 0 && ((n & (n - 1)) == 0));
}

static inline __attribute__((const))
int __ilog2_u64(unsigned long n)
{
  return fls64(n) - 1;
}

/**
 * ilog2 - log of base 2 of 32-bit or a 64-bit unsigned value
 * @n - parameter
 *
 * constant-capable log of base 2 calculation
 * - this can be used to initialise global variables from constant data, hence
 *   the massive ternary operator construction
 *
 * selects the appropriately-sized optimised version depending on sizeof(n)
 */
#define ilog2(n)                                                \
  (                                                             \
   __builtin_constant_p(n) ? (                                  \
                              (n) < 1 ? ____ilog2_NaN() :	\
                              (n) & (1ULL << 63) ? 63 :         \
                              (n) & (1ULL << 62) ? 62 :         \
                              (n) & (1ULL << 61) ? 61 :         \
                              (n) & (1ULL << 60) ? 60 :         \
                              (n) & (1ULL << 59) ? 59 :         \
                              (n) & (1ULL << 58) ? 58 :         \
                              (n) & (1ULL << 57) ? 57 :         \
                              (n) & (1ULL << 56) ? 56 :         \
                              (n) & (1ULL << 55) ? 55 :         \
                              (n) & (1ULL << 54) ? 54 :         \
                              (n) & (1ULL << 53) ? 53 :         \
                              (n) & (1ULL << 52) ? 52 :         \
                              (n) & (1ULL << 51) ? 51 :         \
                              (n) & (1ULL << 50) ? 50 :         \
                              (n) & (1ULL << 49) ? 49 :         \
                              (n) & (1ULL << 48) ? 48 :         \
                              (n) & (1ULL << 47) ? 47 :         \
                              (n) & (1ULL << 46) ? 46 :         \
                              (n) & (1ULL << 45) ? 45 :         \
                              (n) & (1ULL << 44) ? 44 :         \
                              (n) & (1ULL << 43) ? 43 :         \
                              (n) & (1ULL << 42) ? 42 :         \
                              (n) & (1ULL << 41) ? 41 :         \
                              (n) & (1ULL << 40) ? 40 :         \
                              (n) & (1ULL << 39) ? 39 :         \
                              (n) & (1ULL << 38) ? 38 :         \
                              (n) & (1ULL << 37) ? 37 :         \
                              (n) & (1ULL << 36) ? 36 :         \
                              (n) & (1ULL << 35) ? 35 :         \
                              (n) & (1ULL << 34) ? 34 :         \
                              (n) & (1ULL << 33) ? 33 :         \
                              (n) & (1ULL << 32) ? 32 :         \
                              (n) & (1ULL << 31) ? 31 :         \
                              (n) & (1ULL << 30) ? 30 :         \
                              (n) & (1ULL << 29) ? 29 :         \
                              (n) & (1ULL << 28) ? 28 :         \
                              (n) & (1ULL << 27) ? 27 :         \
                              (n) & (1ULL << 26) ? 26 :         \
                              (n) & (1ULL << 25) ? 25 :         \
                              (n) & (1ULL << 24) ? 24 :         \
                              (n) & (1ULL << 23) ? 23 :         \
                              (n) & (1ULL << 22) ? 22 :         \
                              (n) & (1ULL << 21) ? 21 :         \
                              (n) & (1ULL << 20) ? 20 :         \
                              (n) & (1ULL << 19) ? 19 :         \
                              (n) & (1ULL << 18) ? 18 :         \
                              (n) & (1ULL << 17) ? 17 :         \
                              (n) & (1ULL << 16) ? 16 :         \
                              (n) & (1ULL << 15) ? 15 :         \
                              (n) & (1ULL << 14) ? 14 :         \
                              (n) & (1ULL << 13) ? 13 :         \
                              (n) & (1ULL << 12) ? 12 :                 \
                              (n) & (1ULL << 11) ? 11 :                 \
                              (n) & (1ULL << 10) ? 10 :                 \
                              (n) & (1ULL <<  9) ?  9 :                 \
                              (n) & (1ULL <<  8) ?  8 :                 \
                              (n) & (1ULL <<  7) ?  7 :                 \
                              (n) & (1ULL <<  6) ?  6 :                 \
                              (n) & (1ULL <<  5) ?  5 :                 \
                              (n) & (1ULL <<  4) ?  4 :                 \
                              (n) & (1ULL <<  3) ?  3 :                 \
                              (n) & (1ULL <<  2) ?  2 :                 \
                              (n) & (1ULL <<  1) ?  1 :                 \
                              (n) & (1ULL <<  0) ?  0 :                 \
                              ____ilog2_NaN()                           \
                                                                ) :     \
   (sizeof(n) <= 4) ?                                                   \
   __ilog2_u32(n) :                                                     \
   __ilog2_u64(n)                                                       \
                                                                )

/*
 * do_div() is NOT a C function. It wants to return
 * two values (the quotient and the remainder), but
 * since that doesn't work very well in C, what it
 * does is:
 *
 * - modifies the 64-bit dividend _in_place_
 * - returns the 32-bit remainder
 *
 * This ends up being the most efficient "calling
 * convention" on x86.
 * This function copies from arch/x86/include/asm/div64.h
 */
#define do_div(n, base)                                                 \
  ({                                                                    \
    unsigned long __upper, __low, __high, __mod, __base;                \
    __base = (base);                                                    \
    if (__builtin_constant_p(__base) && is_power_of_2(__base)) {        \
      __mod = n & (__base - 1);                                         \
      n >>= ilog2(__base);                                              \
    } else {                                                            \
      asm("" : "=a" (__low), "=d" (__high) : "A" (n));                  \
      __upper = __high;                                                 \
      if (__high) {                                                     \
        __upper = __high % (__base);                                    \
        __high = __high / (__base);                                     \
      }                                                                 \
      asm("divq %2" : "=a" (__low), "=d" (__mod)                        \
          : "rm" (__base), "0" (__low), "1" (__upper));                 \
      asm("" : "=A" (n) : "a" (__low), "d" (__high));                   \
    }                                                                   \
    __mod;                                                              \
  })

/* Decimal conversion is by far the most typical, and is used
 * for /proc and /sys data. This directly impacts e.g. top performance
 * with many processes running. We optimize it for speed
 * using code from
 * http://www.cs.uiowa.edu/~jones/bcd/decimal.html
 * (with permission from the author, Douglas W. Jones). */

/* Formats correctly any integer in [0,99999].
 * Outputs from one to five digits depending on input.
 * On i386 gcc 4.1.2 -O2: ~250 bytes of code. */
static char *put_dec_trunc(char *buf, unsigned q)
{
  unsigned d3, d2, d1, d0;
  d1 = (q>>4) & 0xf;
  d2 = (q>>8) & 0xf;
  d3 = (q>>12);

  d0 = 6*(d3 + d2 + d1) + (q & 0xf);
  q = (d0 * 0xcd) >> 11;
  d0 = d0 - 10*q;
  *buf++ = d0 + '0'; /* least significant digit */
  d1 = q + 9*d3 + 5*d2 + d1;
  if (d1 != 0) {
    q = (d1 * 0xcd) >> 11;
    d1 = d1 - 10*q;
    *buf++ = d1 + '0'; /* next digit */

    d2 = q + 2*d2;
    if ((d2 != 0) || (d3 != 0)) {
      q = (d2 * 0xd) >> 7;
      d2 = d2 - 10*q;
      *buf++ = d2 + '0'; /* next digit */

      d3 = q + 4*d3;
      if (d3 != 0) {
        q = (d3 * 0xcd) >> 11;
        d3 = d3 - 10*q;
        *buf++ = d3 + '0';  /* next digit */
        if (q != 0)
          *buf++ = q + '0'; /* most sign. digit */
      }
    }
  }

  return buf;
}

/* Same with if's removed. Always emits five digits */
static char *put_dec_full(char *buf, unsigned q)
{
  /* BTW, if q is in [0,9999], 8-bit ints will be enough, */
  /* but anyway, gcc produces better code with full-sized ints */
  unsigned d3, d2, d1, d0;
  d1 = (q>>4) & 0xf;
  d2 = (q>>8) & 0xf;
  d3 = (q>>12);

  /*
   * Possible ways to approx. divide by 10
   * gcc -O2 replaces multiply with shifts and adds
   * (x * 0xcd) >> 11: 11001101 - shorter code than * 0x67 (on i386)
   * (x * 0x67) >> 10:  1100111
   * (x * 0x34) >> 9:    110100 - same
   * (x * 0x1a) >> 8:     11010 - same
   * (x * 0x0d) >> 7:      1101 - same, shortest code (on i386)
   */
  d0 = 6*(d3 + d2 + d1) + (q & 0xf);
  q = (d0 * 0xcd) >> 11;
  d0 = d0 - 10*q;
  *buf++ = d0 + '0';
  d1 = q + 9*d3 + 5*d2 + d1;
  q = (d1 * 0xcd) >> 11;
  d1 = d1 - 10*q;
  *buf++ = d1 + '0';

  d2 = q + 2*d2;
  q = (d2 * 0xd) >> 7;
  d2 = d2 - 10*q;
  *buf++ = d2 + '0';

  d3 = q + 4*d3;
  q = (d3 * 0xcd) >> 11; /* - shorter code */
  /* q = (d3 * 0x67) >> 10; - would also work */
  d3 = d3 - 10*q;
  *buf++ = d3 + '0';
  *buf++ = q + '0';

  return buf;
}
/* No inlining helps gcc to use registers better */
static char *put_dec(char *buf, unsigned long long num)
{
  while (1) {
    unsigned rem;
    if (num < 100000)
      return put_dec_trunc(buf, num);
    rem = do_div(num, 100000);
    buf = put_dec_full(buf, rem);
  }
}

#define ZEROPAD	1		/* pad with zero */
#define SIGN	2		/* unsigned/signed long */
#define PLUS	4		/* show plus */
#define SPACE	8		/* space if plus */
#define LEFT	16		/* left justified */
#define SMALL	32		/* use lowercase in hex (must be 32 == 0x20) */
#define SPECIAL	64		/* prefix hex with "0x", octal with "0" */

enum format_type {
  FORMAT_TYPE_NONE, /* Just a string part */
  FORMAT_TYPE_WIDTH,
  FORMAT_TYPE_PRECISION,
  FORMAT_TYPE_CHAR,
  FORMAT_TYPE_STR,
  FORMAT_TYPE_PTR,
  FORMAT_TYPE_PERCENT_CHAR,
  FORMAT_TYPE_INVALID,
  FORMAT_TYPE_LONG_LONG,
  FORMAT_TYPE_ULONG,
  FORMAT_TYPE_LONG,
  FORMAT_TYPE_UBYTE,
  FORMAT_TYPE_BYTE,
  FORMAT_TYPE_USHORT,
  FORMAT_TYPE_SHORT,
  FORMAT_TYPE_UINT,
  FORMAT_TYPE_INT,
  FORMAT_TYPE_NRCHARS,
  FORMAT_TYPE_SIZE_T,
  FORMAT_TYPE_PTRDIFF
};

struct printf_spec {
  u8	type;		/* format_type enum */
  u8	flags;		/* flags to number() */
  u8	base;		/* number base, 8, 10 or 16 only */
  u8	qualifier;	/* number qualifier, one of 'hHlLtzZ' */
  s16	field_width;	/* width of output field */
  s16	precision;	/* # of digits/chars */
};

static char *number(char *buf, char *end, unsigned long long num,
                    struct printf_spec spec)
{
  /* we are called with base 8, 10 or 16, only, thus don't need "G..."  */
  static const char digits[16] = "0123456789ABCDEF"; /* "GHIJKLMNOPQRSTUVWXYZ"; */

  char tmp[66];
  char sign;
  char locase;
  int need_pfx = ((spec.flags & SPECIAL) && spec.base != 10);
  int i;

  /* locase = 0 or 0x20. ORing digits or letters with 'locase'
   * produces same digits or (maybe lowercased) letters */
  locase = (spec.flags & SMALL);
  if (spec.flags & LEFT)
    spec.flags &= ~ZEROPAD;
  sign = 0;
  if (spec.flags & SIGN) {
    if ((signed long long)num < 0) {
      sign = '-';
      num = -(signed long long)num;
      spec.field_width--;
    } else if (spec.flags & PLUS) {
      sign = '+';
      spec.field_width--;
    } else if (spec.flags & SPACE) {
      sign = ' ';
      spec.field_width--;
    }
  }
  if (need_pfx) {
    spec.field_width--;
    if (spec.base == 16)
      spec.field_width--;
  }

  /* generate full string in tmp[], in reverse order */
  i = 0;
  if (num == 0)
    tmp[i++] = '0';
  /* Generic code, for any base:
     else do {
     tmp[i++] = (digits[do_div(num,base)] | locase);
     } while (num != 0);
  */
  else if (spec.base != 10) { /* 8 or 16 */
    int mask = spec.base - 1;
    int shift = 3;

    if (spec.base == 16)
      shift = 4;
    do {
      tmp[i++] = (digits[((unsigned char)num) & mask] | locase);
      num >>= shift;
    } while (num);
  } else { /* base 10 */
    i = put_dec(tmp, num) - tmp;
    /* TODO: figure out why put_dec malfunctions when we try to output
             decimals that are at least 6 digits. The following hack
             seems to work, so I doubt there might be some register
             clobber issues introduced by optimizations.
    */
    write(-1, "", 0);
  }

  /* printing 100 using %2d gives "100", not "00" */
  if (i > spec.precision)
    spec.precision = i;
  /* leading space padding */
  spec.field_width -= spec.precision;
  if (!(spec.flags & (ZEROPAD+LEFT))) {
    while (--spec.field_width >= 0) {
      if (buf < end)
        *buf = ' ';
      ++buf;
    }
  }
  /* sign */
  if (sign) {
    if (buf < end)
      *buf = sign;
    ++buf;
  }
  /* "0x" / "0" prefix */
  if (need_pfx) {
    if (buf < end)
      *buf = '0';
    ++buf;
    if (spec.base == 16) {
      if (buf < end)
        *buf = ('X' | locase);
      ++buf;
    }
  }
  /* zero or space padding */
  if (!(spec.flags & LEFT)) {
    char c = (spec.flags & ZEROPAD) ? '0' : ' ';
    while (--spec.field_width >= 0) {
      if (buf < end)
        *buf = c;
      ++buf;
    }
  }
  /* hmm even more zero padding? */
  while (i <= --spec.precision) {
    if (buf < end)
      *buf = '0';
    ++buf;
  }
  /* actual digits of result */
  while (--i >= 0) {
    if (buf < end)
      *buf = tmp[i];
    ++buf;
  }
  /* trailing space padding */
  while (--spec.field_width >= 0) {
    if (buf < end)
      *buf = ' ';
    ++buf;
  }

  return buf;
}

static char *string(char *buf, char *end, const char *s, struct printf_spec spec)
{
  int len, i;

  if ((unsigned long)s < PAGE_SIZE)
    s = "(null)";

  len = strnlen(s, spec.precision);

  if (!(spec.flags & LEFT)) {
    while (len < spec.field_width--) {
      if (buf < end)
        *buf = ' ';
      ++buf;
    }
  }
  for (i = 0; i < len; ++i) {
    if (buf < end)
      *buf = *s;
    ++buf; ++s;
  }
  while (len < spec.field_width--) {
    if (buf < end)
      *buf = ' ';
    ++buf;
  }

  return buf;
}

/*
 * Show a '%p' thing.  A kernel extension is that the '%p' is followed
 * by an extra set of alphanumeric characters that are extended format
 * specifiers.
 */
static char *pointer(const char *fmt, char *buf, char *end, void *ptr,
	      struct printf_spec spec)
{
  if (!ptr) {
    /*
     * Print (null) with the same width as a pointer so it makes
     * tabular output look nice.
     */
    if (spec.field_width == -1)
      spec.field_width = 2 * sizeof(void *);
    return string(buf, end, "(null)", spec);
  }

  spec.flags |= SMALL;
  if (spec.field_width == -1) {
    spec.field_width = 2 * sizeof(void *);
    spec.flags |= ZEROPAD;
  }
  spec.base = 16;

  return number(buf, end, (unsigned long) ptr, spec);
}

/*
 * Helper function to decode printf style format.
 * Each call decode a token from the format and return the
 * number of characters read (or likely the delta where it wants
 * to go on the next call).
 * The decoded token is returned through the parameters
 *
 * 'h', 'l', or 'L' for integer fields
 * 'z' support added 23/7/1999 S.H.
 * 'z' changed to 'Z' --davidm 1/25/99
 * 't' added for ptrdiff_t
 *
 * @fmt: the format string
 * @type of the token returned
 * @flags: various flags such as +, -, # tokens..
 * @field_width: overwritten width
 * @base: base of the number (octal, hex, ...)
 * @precision: precision of a number
 * @qualifier: qualifier of a number (long, size_t, ...)
 */
static int format_decode(const char *fmt, struct printf_spec *spec)
{
  const char *start = fmt;

  /* we finished early by reading the field width */
  if (spec->type == FORMAT_TYPE_WIDTH) {
    if (spec->field_width < 0) {
      spec->field_width = -spec->field_width;
      spec->flags |= LEFT;
    }
    spec->type = FORMAT_TYPE_NONE;
    goto precision;
  }

  /* we finished early by reading the precision */
  if (spec->type == FORMAT_TYPE_PRECISION) {
    if (spec->precision < 0)
      spec->precision = 0;

    spec->type = FORMAT_TYPE_NONE;
    goto qualifier;
  }

  /* By default */
  spec->type = FORMAT_TYPE_NONE;

  for (; *fmt ; ++fmt) {
    if (*fmt == '%')
      break;
  }

  /* Return the current non-format string */
  if (fmt != start || !*fmt)
    return fmt - start;

  /* Process flags */
  spec->flags = 0;

  while (1) { /* this also skips first '%' */
    bool found = true;

    ++fmt;

    switch (*fmt) {
    case '-': spec->flags |= LEFT;    break;
    case '+': spec->flags |= PLUS;    break;
    case ' ': spec->flags |= SPACE;   break;
    case '#': spec->flags |= SPECIAL; break;
    case '0': spec->flags |= ZEROPAD; break;
    default:  found = false;
    }

    if (!found)
      break;
  }

  /* get field width */
  spec->field_width = -1;

  if (isdigit(*fmt))
    spec->field_width = skip_atoi(&fmt);
  else if (*fmt == '*') {
    /* it's the next argument */
    spec->type = FORMAT_TYPE_WIDTH;
    return ++fmt - start;
  }

 precision:
  /* get the precision */
  spec->precision = -1;
  if (*fmt == '.') {
    ++fmt;
    if (isdigit(*fmt)) {
      spec->precision = skip_atoi(&fmt);
      if (spec->precision < 0)
        spec->precision = 0;
    } else if (*fmt == '*') {
      /* it's the next argument */
      spec->type = FORMAT_TYPE_PRECISION;
      return ++fmt - start;
    }
  }

 qualifier:
  /* get the conversion qualifier */
  spec->qualifier = -1;
  if (*fmt == 'h' || tolower(*fmt) == 'l' ||
      tolower(*fmt) == 'z' || *fmt == 't') {
    spec->qualifier = *fmt++;
    if (spec->qualifier == *fmt) {
      if (spec->qualifier == 'l') {
        spec->qualifier = 'L';
        ++fmt;
      } else if (spec->qualifier == 'h') {
        spec->qualifier = 'H';
        ++fmt;
      }
    }
  }

  /* default base */
  spec->base = 10;
  switch (*fmt) {
  case 'c':
    spec->type = FORMAT_TYPE_CHAR;
    return ++fmt - start;

  case 's':
    spec->type = FORMAT_TYPE_STR;
    return ++fmt - start;

  case 'p':
    spec->type = FORMAT_TYPE_PTR;
    return fmt - start;
    /* skip alnum */

  case 'n':
    spec->type = FORMAT_TYPE_NRCHARS;
    return ++fmt - start;

  case '%':
    spec->type = FORMAT_TYPE_PERCENT_CHAR;
    return ++fmt - start;

    /* integer number formats - set up the flags and "break" */
  case 'o':
    spec->base = 8;
    break;

  case 'x':
    spec->flags |= SMALL;

  case 'X':
    spec->base = 16;
    break;

  case 'd':
  case 'i':
    spec->flags |= SIGN;
  case 'u':
    break;

  default:
    spec->type = FORMAT_TYPE_INVALID;
    return fmt - start;
  }

  if (spec->qualifier == 'L')
    spec->type = FORMAT_TYPE_LONG_LONG;
  else if (spec->qualifier == 'l') {
    if (spec->flags & SIGN)
      spec->type = FORMAT_TYPE_LONG;
    else
      spec->type = FORMAT_TYPE_ULONG;
  } else if (tolower(spec->qualifier) == 'z') {
    spec->type = FORMAT_TYPE_SIZE_T;
  } else if (spec->qualifier == 't') {
    spec->type = FORMAT_TYPE_PTRDIFF;
  } else if (spec->qualifier == 'H') {
    if (spec->flags & SIGN)
      spec->type = FORMAT_TYPE_BYTE;
    else
      spec->type = FORMAT_TYPE_UBYTE;
  } else if (spec->qualifier == 'h') {
    if (spec->flags & SIGN)
      spec->type = FORMAT_TYPE_SHORT;
    else
      spec->type = FORMAT_TYPE_USHORT;
  } else {
    if (spec->flags & SIGN)
      spec->type = FORMAT_TYPE_INT;
    else
      spec->type = FORMAT_TYPE_UINT;
  }

  return ++fmt - start;
}

/**
 * vsnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * This function follows C99 vsnprintf:
 * The return value is the number of characters which would
 * be generated for the given input, excluding the trailing
 * '\0', as per ISO C99. If you want to have the exact
 * number of characters written into @buf as return value
 * (not including the trailing '\0'), use vscnprintf(). If the
 * return is greater than or equal to @size, the resulting
 * string is truncated.
 *
 * If you're not already dealing with a va_list consider using snprintf().
 */
int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
  unsigned long long num;
  char *str, *end;
  struct printf_spec spec = {0};

  /* Reject out-of-range values early.  Large positive sizes are
     used for unknown buffer sizes. */
  if ((int) size < 0)
    return 0;

  str = buf;
  end = buf + size;

  /* Make sure end is always >= buf */
  if (end < buf) {
    end = ((void *)-1);
    size = end - buf;
  }

  while (*fmt) {
    const char *old_fmt = fmt;
    int read = format_decode(fmt, &spec);

    fmt += read;

    switch (spec.type) {
    case FORMAT_TYPE_NONE: {
      int copy = read;
      if (str < end) {
        if (copy > end - str)
          copy = end - str;
        memcpy(str, old_fmt, copy);
      }
      str += read;
      break;
    }

    case FORMAT_TYPE_WIDTH:
      spec.field_width = va_arg(args, int);
      break;

    case FORMAT_TYPE_PRECISION:
      spec.precision = va_arg(args, int);
      break;

    case FORMAT_TYPE_CHAR: {
      char c;

      if (!(spec.flags & LEFT)) {
        while (--spec.field_width > 0) {
          if (str < end)
            *str = ' ';
          ++str;

        }
      }
      c = (unsigned char) va_arg(args, int);
      if (str < end)
        *str = c;
      ++str;
      while (--spec.field_width > 0) {
        if (str < end)
          *str = ' ';
        ++str;
      }
      break;
    }

    case FORMAT_TYPE_STR:
      str = string(str, end, va_arg(args, char *), spec);
      break;

    case FORMAT_TYPE_PTR:
      str = pointer(fmt+1, str, end, va_arg(args, void *),
                    spec);
      while (isalnum(*fmt))
        fmt++;
      break;

    case FORMAT_TYPE_PERCENT_CHAR:
      if (str < end)
        *str = '%';
      ++str;
      break;

    case FORMAT_TYPE_INVALID:
      if (str < end)
        *str = '%';
      ++str;
      break;

    case FORMAT_TYPE_NRCHARS: {
      u8 qualifier = spec.qualifier;

      if (qualifier == 'l') {
        long *ip = va_arg(args, long *);
        *ip = (str - buf);
      } else if (tolower(qualifier) == 'z') {
        size_t *ip = va_arg(args, size_t *);
        *ip = (str - buf);
      } else {
        int *ip = va_arg(args, int *);
        *ip = (str - buf);
      }
      break;
    }

    default:
      switch (spec.type) {
      case FORMAT_TYPE_LONG_LONG:
        num = va_arg(args, long long);
        break;
      case FORMAT_TYPE_ULONG:
        num = va_arg(args, unsigned long);
        break;
      case FORMAT_TYPE_LONG:
        num = va_arg(args, long);
        break;
      case FORMAT_TYPE_SIZE_T:
        num = va_arg(args, size_t);
        break;
      case FORMAT_TYPE_PTRDIFF:
        num = va_arg(args, ptrdiff_t);
        break;
      case FORMAT_TYPE_UBYTE:
        num = (unsigned char) va_arg(args, int);
        break;
      case FORMAT_TYPE_BYTE:
        num = (signed char) va_arg(args, int);
        break;
      case FORMAT_TYPE_USHORT:
        num = (unsigned short) va_arg(args, int);
        break;
      case FORMAT_TYPE_SHORT:
        num = (short) va_arg(args, int);
        break;
      case FORMAT_TYPE_INT:
        num = (int) va_arg(args, int);
        break;
      default:
        num = va_arg(args, unsigned int);
      }

      str = number(str, end, num, spec);
    }
  }

  if (size > 0) {
    if (str < end)
      *str = '\0';
    else
      end[-1] = '\0';
  }

  /* the trailing null byte doesn't count towards the total */
  return str-buf;

}

/**
 * vscnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The return value is the number of characters which have been written into
 * the @buf not including the trailing '\0'. If @size is == 0 the function
 * returns 0.
 *
 * If you're not already dealing with a va_list consider using scnprintf().
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int vscnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
  int i;

  i = vsnprintf(buf, size, fmt, args);

  if (i < size)
    return i;
  if (size != 0)
    return size - 1;
  return 0;
}

/**
 * snprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters which would be
 * generated for the given input, excluding the trailing null,
 * as per ISO C99.  If the return is greater than or equal to
 * @size, the resulting string is truncated.
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list args;
  int i;

  va_start(args, fmt);
  i = vsnprintf(buf, size, fmt, args);
  va_end(args);

  return i;
}

/**
 * scnprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @size: The size of the buffer, including the trailing null space
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The return value is the number of characters written into @buf not including
 * the trailing '\0'. If @size is == 0 the function returns 0.
 */

int scnprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list args;
  int i;

  va_start(args, fmt);
  i = vscnprintf(buf, size, fmt, args);
  va_end(args);

  return i;
}

/**
 * vsprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @args: Arguments for the format string
 *
 * The function returns the number of characters written
 * into @buf. Use vsnprintf() or vscnprintf() in order to avoid
 * buffer overflows.
 *
 * If you're not already dealing with a va_list consider using sprintf().
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int vsprintf(char *buf, const char *fmt, va_list args)
{
  return vsnprintf(buf, INT_MAX, fmt, args);
}

/**
 * sprintf - Format a string and place it in a buffer
 * @buf: The buffer to place the result into
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The function returns the number of characters written
 * into @buf. Use snprintf() or scnprintf() in order to avoid
 * buffer overflows.
 *
 * See the vsnprintf() documentation for format string extensions over C99.
 */
int sprintf(char *buf, const char *fmt, ...)
{
  va_list args;
  int i;

  va_start(args, fmt);
  i = vsnprintf(buf, INT_MAX, fmt, args);
  va_end(args);

  return i;
}

/**
 * dprintf - Format a string and output it to a file
 * @fd: The file descriptor
 * @fmt: The format string to use
 * @...: Arguments for the format string
 *
 * The function returns the number of characters written
 * into @buf.
 *
 */
int dprintf(int fd, const char *fmt, ...) {
  va_list args;
  /* we can only print out strings that are no more than 16KB long. */
  char buf[PAGE_SIZE * 4];
  buf[PAGE_SIZE * 4 - 1] = '\0';
  int i;
  va_start(args, fmt);
  /* note that only PAGE_SIZE - 2 bytes are allowed */
  i = vsnprintf(buf, PAGE_SIZE * 4 - 1, fmt, args);
  va_end(args);
  write(fd, buf, i);
  return i;
}
