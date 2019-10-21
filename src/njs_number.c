
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) NGINX, Inc.
 */


#include <njs_main.h>
#include <njs_diyfp.h>


/*
 * 2^53 - 1 is the largest integer n such that n and n + 1
 * as well as -n and -n - 1 are all exactly representable
 * in the IEEE-754 format.
 */
#define NJS_MAX_SAFE_INTEGER  ((1LL << 53) - 1)


static njs_int_t njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix);


uint32_t
njs_value_to_index(const njs_value_t *value)
{
    double       num;
    njs_array_t  *array;

    num = NAN;

    if (njs_fast_path(njs_is_numeric(value))) {
        num = njs_number(value);

    } else if (njs_is_string(value)) {
        num = njs_string_to_index(value);

    } else if (njs_is_array(value)) {

        array = njs_array(value);

        if (njs_lvlhsh_is_empty(&array->object.hash)) {

            if (array->length == 0) {
                /* An empty array value is zero. */
                return 0;
            }

            if (array->length == 1 && njs_is_valid(&array->start[0])) {
                /* A single value array is the zeroth array value. */
                return njs_value_to_index(&array->start[0]);
            }
        }
    }

    if ((uint32_t) num == num) {
        return (uint32_t) num;
    }

    return NJS_ARRAY_INVALID_INDEX;
}


double
njs_number_dec_parse(const u_char **start, const u_char *end)
{
    return njs_strtod(start, end);
}


uint64_t
njs_number_oct_parse(const u_char **start, const u_char *end)
{
    u_char        c;
    uint64_t      num;
    const u_char  *p;

    p = *start;

    num = 0;

    while (p < end) {
        /* Values less than '0' become >= 208. */
        c = *p - '0';

        if (njs_slow_path(c > 7)) {
            break;
        }

        num = num * 8 + c;
        p++;
    }

    *start = p;

    return num;
}


uint64_t
njs_number_bin_parse(const u_char **start, const u_char *end)
{
    u_char        c;
    uint64_t      num;
    const u_char  *p;

    p = *start;

    num = 0;

    while (p < end) {
        /* Values less than '0' become >= 208. */
        c = *p - '0';

        if (njs_slow_path(c > 1)) {
            break;
        }

        num = num * 2 + c;
        p++;
    }

    *start = p;

    return num;
}


uint64_t
njs_number_hex_parse(const u_char **start, const u_char *end)
{
    uint64_t      num;
    njs_int_t     n;
    const u_char  *p;

    p = *start;

    num = 0;

    while (p < end) {
        n = njs_char_to_hex(*p);
        if (njs_slow_path(n < 0)) {
            break;
        }

        num = num * 16 + n;
        p++;
    }

    *start = p;

    return num;
}


int64_t
njs_number_radix_parse(const u_char **start, const u_char *end, uint8_t radix)
{
    uint8_t       d;
    int64_t       num;
    uint64_t      n;
    const u_char  *p;

    static const int8_t  digits[256]
        njs_aligned(32) =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
         0,  1,  2,  3,  4,  5,  6,  7,  8,  9, -1, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
        -1, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };

    num = -1;
    n = 0;

    for (p = *start; p < end; p++) {
        d = digits[*p];

        if (njs_slow_path(d >= radix)) {
            break;
        }

        n = (n * radix) + d;
        num = n;
    }

    *start = p;

    return num;
}


njs_int_t
njs_number_to_string(njs_vm_t *vm, njs_value_t *string,
    const njs_value_t *number)
{
    double             num;
    size_t             size;
    const njs_value_t  *value;
    u_char             buf[128];

    num = njs_number(number);

    if (isnan(num)) {
        value = &njs_string_nan;

    } else if (isinf(num)) {

        if (num < 0) {
            value = &njs_string_minus_infinity;

        } else {
            value = &njs_string_plus_infinity;
        }

    } else {
        size = njs_dtoa(num, (char *) buf);

        return njs_string_new(vm, string, buf, size, size);
    }

    *string = *value;

    return NJS_OK;
}


njs_int_t
njs_number_constructor(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_object_t       *object;
    const njs_value_t  *value;

    if (nargs == 1) {
        value = &njs_value_zero;

    } else {
        value = &args[1];
    }

    if (vm->top_frame->ctor) {
        object = njs_object_value_alloc(vm, value, NJS_NUMBER);
        if (njs_slow_path(object == NULL)) {
            return NJS_ERROR;
        }

        njs_set_type_object(&vm->retval, object, NJS_OBJECT_NUMBER);

    } else {
        njs_set_number(&vm->retval, njs_number(value));
    }

    return NJS_OK;
}


static njs_int_t
njs_number_is_integer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (num == trunc(num) && !isinf(num)) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}



static njs_int_t
njs_number_is_safe_integer(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (num == (int64_t) num && fabs(num) <= NJS_MAX_SAFE_INTEGER) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_number_is_nan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1
        && njs_is_number(&args[1])
        && isnan(njs_number(&args[1])))
    {
        value = &njs_value_true;
    }

    vm->retval = *value;

    return NJS_OK;
}


static const njs_object_prop_t  njs_number_constructor_properties[] =
{
    /* Number.name == "Number". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("Number"),
        .configurable = 1,
    },

    /* Number.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },

    /* Number.prototype. */
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("prototype"),
        .value = njs_prop_handler(njs_object_prototype_create),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("EPSILON"),
        .value = njs_value(NJS_NUMBER, 1, DBL_EPSILON),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("MAX_SAFE_INTEGER"),
        .value = njs_value(NJS_NUMBER, 1, NJS_MAX_SAFE_INTEGER),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("MIN_SAFE_INTEGER"),
        .value = njs_value(NJS_NUMBER, 1, -NJS_MAX_SAFE_INTEGER),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("MAX_VALUE"),
        .value = njs_value(NJS_NUMBER, 1, DBL_MAX),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("MIN_VALUE"),
        .value = njs_value(NJS_NUMBER, 1, DBL_MIN),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("NaN"),
        .value = njs_value(NJS_NUMBER, 0, NAN),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("POSITIVE_INFINITY"),
        .value = njs_value(NJS_NUMBER, 1, INFINITY),
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_long_string("NEGATIVE_INFINITY"),
        .value = njs_value(NJS_NUMBER, 1, -INFINITY),
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("isFinite"),
        .value = njs_native_function(njs_number_is_finite, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("isInteger"),
        .value = njs_native_function(njs_number_is_integer, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("isSafeInteger"),
        .value = njs_native_function(njs_number_is_safe_integer, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("isNaN"),
        .value = njs_native_function(njs_number_is_nan, 1, 0),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("parseFloat"),
        .value = njs_native_function(njs_number_parse_float, 1,
                                     NJS_SKIP_ARG, NJS_STRING_ARG),
        .writable = 1,
        .configurable = 1,
    },

    /* ES6. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("parseInt"),
        .value = njs_native_function(njs_number_parse_int, 2,
                     NJS_SKIP_ARG, NJS_STRING_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_number_constructor_init = {
    njs_str("Number"),
    njs_number_constructor_properties,
    njs_nitems(njs_number_constructor_properties),
};


static njs_int_t
njs_number_prototype_value_of(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


static njs_int_t
njs_number_prototype_to_string(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double       number, radix;
    njs_value_t  *value;

    value = &args[0];

    if (value->type != NJS_NUMBER) {

        if (value->type == NJS_OBJECT_NUMBER) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    if (nargs > 1) {
        radix = njs_number(&args[1]);

        if (radix < 2 || radix > 36 || radix != (int) radix) {
            njs_range_error(vm, NULL);
            return NJS_ERROR;
        }

        number = njs_number(value);

        if (radix != 10 && !isnan(number) && !isinf(number)) {
            return njs_number_to_string_radix(vm, &vm->retval, number, radix);
        }
    }

    return njs_number_to_string(vm, &vm->retval, value);
}


static njs_int_t
njs_number_prototype_to_fixed(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    u_char       *p;
    int64_t      frac;
    double       number;
    size_t       length, size;
    njs_int_t    ret, point, prefix, postfix;
    njs_value_t  *value;
    u_char       buf[128], buf2[128];

    /* 128 > 100 + 21 + njs_length(".-\0"). */

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (value->type == NJS_OBJECT_NUMBER) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    ret = njs_value_to_integer(vm, njs_arg(args, nargs, 1), &frac);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(frac < 0 || frac > 100)) {
        njs_range_error(vm, "digits argument must be between 0 and 100");
        return NJS_ERROR;
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || fabs(number) >= 1e21)) {
        return njs_number_to_string(vm, &vm->retval, value);
    }

    point = 0;
    length = njs_fixed_dtoa(number, (njs_int_t) frac, (char *) buf, &point);

    prefix = 0;
    postfix = 0;

    if (point <= 0) {
        prefix = -point + 1;
        point = 1;
    }

    if (prefix + (njs_int_t) length < point + frac) {
        postfix = point + frac - length - prefix;
    }

    size = prefix + length + postfix + !!(number < 0);

    if (frac > 0) {
        size += njs_length(".");
    }

    p = buf2;

    while (--prefix >= 0) {
        *p++ = '0';
    }

    if (length != 0) {
        p = njs_cpymem(p, buf, length);
    }

    while (--postfix >= 0) {
        *p++ = '0';
    }

    p = njs_string_alloc(vm, &vm->retval, size, size);
    if (njs_slow_path(p == NULL)) {
        return NJS_ERROR;
    }

    if (number < 0) {
        *p++ = '-';
    }

    p = njs_cpymem(p, buf2, point);

    if (frac > 0) {
        *p++ = '.';

        p = njs_cpymem(p, &buf2[point], frac);
    }

    return NJS_OK;
}


static njs_int_t
njs_number_prototype_to_precision(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double       number;
    size_t       size;
    int64_t      precision;
    njs_int_t    ret;
    njs_value_t  *value;
    u_char       buf[128];

    /* 128 > 100 + 21 + njs_length(".-\0"). */

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (value->type == NJS_OBJECT_NUMBER) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    if (njs_is_undefined(njs_arg(args, nargs, 1))) {
        return njs_number_to_string(vm, &vm->retval, value);
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || isinf(number))) {
        return njs_number_to_string(vm, &vm->retval, value);
    }

    ret = njs_value_to_integer(vm, njs_argument(args, 1), &precision);
    if (njs_slow_path(ret != NJS_OK)) {
        return ret;
    }

    if (njs_slow_path(precision < 1 || precision > 100)) {
        njs_range_error(vm, "precision argument must be between 1 and 100");
        return NJS_ERROR;
    }

    size = njs_dtoa_precision(number, (char *) buf, (size_t) precision);

    return njs_string_new(vm, &vm->retval, buf, size, size);
}


static njs_int_t
njs_number_prototype_to_exponential(njs_vm_t *vm, njs_value_t *args,
    njs_uint_t nargs, njs_index_t unused)
{
    double       number;
    size_t       size;
    int64_t      frac;
    njs_int_t    ret;
    njs_value_t  *value;
    u_char       buf[128];

    value = &args[0];

    if (value->type != NJS_NUMBER) {
        if (value->type == NJS_OBJECT_NUMBER) {
            value = njs_object_value(value);

        } else {
            njs_type_error(vm, "unexpected value type:%s",
                           njs_type_string(value->type));
            return NJS_ERROR;
        }
    }

    number = njs_number(value);

    if (njs_slow_path(isnan(number) || isinf(number))) {
        return njs_number_to_string(vm, &vm->retval, value);
    }

    if (njs_is_defined(njs_arg(args, nargs, 1))) {
        ret = njs_value_to_integer(vm, njs_argument(args, 1), &frac);
        if (njs_slow_path(ret != NJS_OK)) {
            return ret;
        }

        if (njs_slow_path(frac < 0 || frac > 100)) {
            njs_range_error(vm, "digits argument must be between 0 and 100");
            return NJS_ERROR;
        }

    } else {
        frac = -1;
    }

    size = njs_dtoa_exponential(number, (char *) buf, (njs_int_t) frac);

    return njs_string_new(vm, &vm->retval, buf, size, size);
}


/*
 * The radix equal to 2 produces the longest  value for a number.
 */

#define NJS_STRING_RADIX_INTERGRAL_LEN  (1 + 1024)
#define NJS_STRING_RADIX_FRACTION_LEN   (1 + 1075)
#define NJS_STRING_RADIX_LEN                                                  \
    (NJS_STRING_RADIX_INTERGRAL_LEN + NJS_STRING_RADIX_FRACTION_LEN)


njs_inline double
njs_number_next_double(double n)
{
    njs_diyfp_t  v;

    v = njs_d2diyfp(n);

    if (signbit(n)) {
        if (v.significand == 0) {
            return 0.0;
        }

        v.significand--;

    } else {
        v.significand++;
    }

    return njs_diyfp2d(v);
}


static njs_int_t
njs_number_to_string_radix(njs_vm_t *vm, njs_value_t *string,
    double number, uint32_t radix)
{
    int       digit;
    char      ch;
    double    n, remainder, integer, fraction, delta;
    u_char    *p, *end;
    uint32_t  size;
    u_char    buf[NJS_STRING_RADIX_LEN];

    static const char  *digits = "0123456789abcdefghijklmnopqrstuvwxyz";

    p = buf + NJS_STRING_RADIX_INTERGRAL_LEN;
    end = p;

    n = number;

    if (n < 0) {
        n = -n;
    }

    integer = floor(n);
    fraction = n - integer;

    delta = 0.5 * (njs_number_next_double(n) - n);
    delta = njs_max(njs_number_next_double(0.0), delta);

    if (fraction >= delta) {
        *p++ = '.';

        do {
            fraction *= radix;
            delta *= radix;

            digit = (int) fraction;
            *p++ = digits[digit];

            fraction -= digit;

            if ((fraction > 0.5 || (fraction == 0.5 && (digit & 1)))
                && (fraction + delta > 1))
            {
                while (p-- != buf) {
                    if (p == buf + NJS_STRING_RADIX_INTERGRAL_LEN) {
                        integer += 1;
                        break;
                    }

                    ch = *p;
                    digit = (ch > '9') ? ch - 'a' + 10 : ch - '0';

                    if ((uint32_t) (digit + 1) < radix) {
                        *p++ = digits[digit + 1];
                        break;
                    }
                }

                break;
            }

        } while (fraction >= delta);

        end = p;
    }

    p = buf + NJS_STRING_RADIX_INTERGRAL_LEN;

    while (njs_d2diyfp(integer / radix).exp > 0) {
        integer /= radix;
        *(--p) = '0';
    }

    do {
        remainder = fmod(integer, radix);
        *(--p) = digits[(int) remainder];
        integer = (integer - remainder) / radix;

    } while (integer > 0);

    if (number < 0) {
        *(--p) = '-';
    }

    size = (uint32_t) (end - p);

    return njs_string_new(vm, string, p, size, size);
}


static const njs_object_prop_t  njs_number_prototype_properties[] =
{
    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("__proto__"),
        .value = njs_prop_handler(njs_primitive_prototype_get_proto),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY_HANDLER,
        .name = njs_string("constructor"),
        .value = njs_prop_handler(njs_object_prototype_create_constructor),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("valueOf"),
        .value = njs_native_function(njs_number_prototype_value_of, 0, 0),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toString"),
        .value = njs_native_function(njs_number_prototype_to_string, 1,
                                     NJS_SKIP_ARG, NJS_NUMBER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toFixed"),
        .value = njs_native_function(njs_number_prototype_to_fixed, 1,
                                     NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toPrecision"),
        .value = njs_native_function(njs_number_prototype_to_precision, 1,
                                     NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },

    {
        .type = NJS_PROPERTY,
        .name = njs_string("toExponential"),
        .value = njs_native_function(njs_number_prototype_to_exponential, 1,
                                     NJS_SKIP_ARG, NJS_INTEGER_ARG),
        .writable = 1,
        .configurable = 1,
    },
};


const njs_object_init_t  njs_number_prototype_init = {
    njs_str("Number"),
    njs_number_prototype_properties,
    njs_nitems(njs_number_prototype_properties),
};


njs_int_t
njs_number_global_is_nan(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    const njs_value_t  *value;

    value = &njs_value_true;

    if (nargs > 1 && !isnan(njs_number(&args[1]))) {
        value = &njs_value_false;
    }

    vm->retval = *value;

    return NJS_OK;
}


njs_int_t
njs_number_is_finite(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    const njs_value_t  *value;

    value = &njs_value_false;

    if (nargs > 1 && njs_is_number(&args[1])) {
        num = njs_number(&args[1]);

        if (!isnan(num) && !isinf(num)) {
            value = &njs_value_true;
        }
    }

    vm->retval = *value;

    return NJS_OK;
}


njs_int_t
njs_number_parse_int(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double             num;
    int64_t            n;
    uint8_t            radix;
    njs_str_t          string;
    njs_bool_t         minus, test_prefix;
    const u_char       *p, *end;

    num = NAN;

    if (nargs > 1) {
        njs_string_get(&args[1], &string);

        end = string.start + string.length;

        for (p = string.start; p < end; p++) {
            if (*p != ' ') {
                goto found;
            }
        }

        goto done;

    found:

        minus = 0;

        if (p[0] == '-') {
            p++;
            minus = 1;

        } else if (p[0] == '+') {
            p++;
        }

        test_prefix = (end - p > 1);
        radix = 0;

        if (nargs > 2) {
            radix = njs_number(&args[2]);

            if (radix != 0) {
                if (radix < 2 || radix > 36) {
                    goto done;
                }

                if (radix != 16) {
                    test_prefix = 0;
                }
            }
        }

        if (radix == 0) {
            radix = 10;
        }

        if (test_prefix && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
            radix = 16;
        }

        n = njs_number_radix_parse(&p, end, radix);

        if (n >= 0) {
            num = n;
            num = minus ? -num : num;
        }
    }

done:

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


njs_int_t
njs_number_parse_float(njs_vm_t *vm, njs_value_t *args, njs_uint_t nargs,
    njs_index_t unused)
{
    double  num;

    num = NAN;

    if (nargs > 1) {
        num = njs_string_to_number(&args[1], 1);
    }

    njs_set_number(&vm->retval, num);

    return NJS_OK;
}


static const njs_object_prop_t  njs_is_finite_function_properties[] =
{
    /* isFinite.name == "isFinite". */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("name"),
        .value = njs_string("isFinite"),
        .configurable = 1,
    },

    /* isFinite.length == 1. */
    {
        .type = NJS_PROPERTY,
        .name = njs_string("length"),
        .value = njs_value(NJS_NUMBER, 1, 1.0),
        .configurable = 1,
    },
};


const njs_object_init_t  njs_is_finite_function_init = {
    njs_str("isFinite"),
    njs_is_finite_function_properties,
    njs_nitems(njs_is_finite_function_properties),
};

