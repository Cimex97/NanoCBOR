/*
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @ingroup nanocbor
 * @{
 * @file
 * @brief   Minimalistic CBOR decoder implementation
 *
 * @author  Koen Zandberg <koen@bergzand.net>
 * @}
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "nanocbor/config.h"
#include "nanocbor/nanocbor.h"

#include NANOCBOR_BYTEORDER_HEADER


void nanocbor_decoder_init(nanocbor_value_t *value,
                           const uint8_t *buf, size_t len)
{
    value->cur = buf;
    value->end = buf + len;
    value->flags = 0;
}

static void _advance(nanocbor_value_t *cvalue, unsigned int res)
{
    cvalue->cur += res;
    cvalue->remaining--;
}

static int _advance_if(nanocbor_value_t *cvalue, int res)
{
    if (res > 0) {
        _advance(cvalue, (unsigned int)res);
    }
    return res;
}

static inline bool _over_end(const nanocbor_value_t *it)
{
    return it->cur >= it->end;
}

static inline uint8_t _get_type(const nanocbor_value_t *value)
{
    return (*value->cur & NANOCBOR_TYPE_MASK);
}

static int _value_match_exact(nanocbor_value_t *cvalue, uint8_t val)
{
    int res = NANOCBOR_ERR_INVALID_TYPE;

    if (_over_end(cvalue)) {
        res = NANOCBOR_ERR_END;
    }
    else if (*cvalue->cur == val) {
        _advance(cvalue, 1U);
        res = 1; /* simple CBOR value is 1 byte */
    }
    return res;
}

bool nanocbor_at_end(const nanocbor_value_t *it)
{
    bool end = false;
    /* The container is at the end when */
    if (_over_end(it) || /* Number of items exhausted */
        /* Indefinite container and the current item is the end marker */
        ((nanocbor_container_indefinite(it) &&
         *it->cur == (NANOCBOR_MASK_FLOAT | NANOCBOR_VALUE_MASK))) ||
        /* Or the remaining number of items is zero */
        (!nanocbor_container_indefinite(it) && nanocbor_in_container(it) && it->remaining == 0)
            ) {
        end = true;
    }
    return end;
}

int nanocbor_get_type(const nanocbor_value_t *value)
{
    if (nanocbor_at_end(value)) {
        return NANOCBOR_ERR_END;
    }
    return (_get_type(value) >> NANOCBOR_TYPE_OFFSET);
}

static int _get_uint64(nanocbor_value_t *cvalue, uint32_t *value, uint8_t max, uint8_t type_mask)
{
    if (nanocbor_at_end(cvalue)) {
        return NANOCBOR_ERR_END;
    }

    uint8_t ctype = _get_type(cvalue);

    if (type_mask != ctype) {
        return NANOCBOR_ERR_INVALID_TYPE;
    }
    unsigned bytelen = *cvalue->cur & NANOCBOR_VALUE_MASK;

    if (bytelen < NANOCBOR_SIZE_BYTE) {
        *value = bytelen;
        /* Ptr should advance 1 pos */
        return 1;
    }
    if (bytelen > max) {
        return NANOCBOR_ERR_OVERFLOW;
    }

    unsigned bytes = 1U << (bytelen - NANOCBOR_SIZE_BYTE);

    if ((cvalue->cur + bytes) >= cvalue->end) {
        return NANOCBOR_ERR_END;
    }
    uint64_t tmp = 0;
    /* Copy the value from cbor to the least significant bytes */
    memcpy(((uint8_t *)&tmp) + sizeof(uint64_t) - bytes, cvalue->cur + 1U, bytes);
    /* NOLINTNEXTLINE: user supplied function */
    tmp = NANOCBOR_BE64TOH_FUNC(tmp);
    memcpy(value, &tmp, bytes);

    return (int)(1 + bytes);
}

static int _get_and_advance_uint8(nanocbor_value_t *cvalue, uint8_t *value,
                                  uint8_t type_mask)
{
    uint32_t tmp = 0;
    int res = _get_uint64(cvalue, &tmp, NANOCBOR_SIZE_BYTE,
                          type_mask);
    *value = tmp;

    return _advance_if(cvalue, res);
}

static int _get_and_advance_uint16(nanocbor_value_t *cvalue, uint16_t *value,
                                   uint8_t type_mask)
{
    uint32_t tmp = 0;
    int res = _get_uint64(cvalue, &tmp, NANOCBOR_SIZE_SHORT,
                          type_mask);
    *value = tmp;

    return _advance_if(cvalue, res);
}

static int _get_and_advance_uint32(nanocbor_value_t *cvalue, uint32_t *value,
                                   uint8_t type_mask)
{
    uint32_t tmp = 0;
    int res = _get_uint64(cvalue, &tmp, NANOCBOR_SIZE_WORD,
                          type_mask);
    *value = tmp;

    return _advance_if(cvalue, res);
}

int nanocbor_get_uint8(nanocbor_value_t *cvalue, uint8_t *value)
{
    return _get_and_advance_uint8(cvalue, value, NANOCBOR_MASK_UINT);
}

int nanocbor_get_uint16(nanocbor_value_t *cvalue, uint16_t *value)
{
    return _get_and_advance_uint16(cvalue, value, NANOCBOR_MASK_UINT);
}

int nanocbor_get_uint32(nanocbor_value_t *cvalue, uint32_t *value)
{
    return _get_and_advance_uint32(cvalue, value, NANOCBOR_MASK_UINT);
}

static int _get_and_advance_int32(nanocbor_value_t *cvalue, int32_t *value, uint8_t max,
                                  uint32_t bound)
{
    if (nanocbor_at_end(cvalue)) {
        return NANOCBOR_ERR_END;
    }
    uint8_t type_mask = _get_type(cvalue);

    int res = NANOCBOR_ERR_INVALID_TYPE;
    if (type_mask == NANOCBOR_MASK_NINT || type_mask == NANOCBOR_MASK_UINT) {
        uint32_t intermediate = 0;
        res = _get_uint64(cvalue, &intermediate, max, type_mask);
        if (intermediate > bound) {
            res = NANOCBOR_ERR_OVERFLOW;
        }
        if (type_mask == NANOCBOR_MASK_NINT) {
            *value = (-(int32_t)intermediate) - 1;
        }
        else {
            *value = intermediate;
        }
    }
    return _advance_if(cvalue, res);
}

int nanocbor_get_int8(nanocbor_value_t *cvalue, int8_t *value)
{
    int32_t tmp = 0;
    int res = _get_and_advance_int32(cvalue, &tmp, NANOCBOR_SIZE_BYTE, INT8_MAX);

    *value = tmp;

    return res;
}

int nanocbor_get_int16(nanocbor_value_t *cvalue, int16_t *value)
{
    int32_t tmp = 0;
    int res = _get_and_advance_int32(cvalue, &tmp, NANOCBOR_SIZE_SHORT, INT16_MAX);

    *value = tmp;

    return res;
}

int nanocbor_get_int32(nanocbor_value_t *cvalue, int32_t *value)
{
    return _get_and_advance_int32(cvalue, value, NANOCBOR_SIZE_WORD, INT32_MAX);
}

int nanocbor_get_tag(nanocbor_value_t *cvalue, uint32_t *tag)
{
    return _get_and_advance_uint32(cvalue, tag, NANOCBOR_MASK_TAG);
}

static int _get_str(nanocbor_value_t *cvalue, const uint8_t **buf, size_t *len, uint8_t type_mask)
{
    *len = 0;
    int res = _get_uint64(cvalue, (uint32_t*)len, NANOCBOR_SIZE_SIZET, type_mask);

    if (cvalue->end - cvalue->cur < 0 || (size_t)(cvalue->end - cvalue->cur) < *len) {
        return NANOCBOR_ERR_END;
    }
    if (res > 0) {
        *buf = (cvalue->cur) + res;
        _advance(cvalue, (unsigned int)(res + *len));
    }
    return res;
}

int nanocbor_get_bstr(nanocbor_value_t *cvalue, const uint8_t **buf, size_t *len)
{
    return _get_str(cvalue, buf, len, NANOCBOR_MASK_BSTR);
}

int nanocbor_get_tstr(nanocbor_value_t *cvalue, const uint8_t **buf, size_t *len)
{
    return _get_str(cvalue, buf, len, NANOCBOR_MASK_TSTR);
}

int nanocbor_get_null(nanocbor_value_t *cvalue)
{
    return _value_match_exact(cvalue, NANOCBOR_MASK_FLOAT | NANOCBOR_SIMPLE_NULL);
}

int nanocbor_get_bool(nanocbor_value_t *cvalue, bool *value)
{
    *value = false;
    int res = _value_match_exact(cvalue, NANOCBOR_MASK_FLOAT | NANOCBOR_SIMPLE_FALSE);
    if (res < 0) {
        *value = true;
        res = _value_match_exact(cvalue, NANOCBOR_MASK_FLOAT | NANOCBOR_SIMPLE_TRUE);
    }
    return res;
}

int _enter_container(nanocbor_value_t *it, nanocbor_value_t *container,
                     uint8_t type_mask)
{
    container->end = it->end;
    container->remaining = 0;

    if (_value_match_exact(it, (type_mask | NANOCBOR_SIZE_INDEFINITE)) == 1) {
        container->flags = NANOCBOR_DECODER_FLAG_INDEFINITE |
                           NANOCBOR_DECODER_FLAG_CONTAINER;
        container->cur = it->cur;
        return NANOCBOR_OK;
    }

    int res = _get_uint64(it, &container->remaining,
                          NANOCBOR_SIZE_WORD, type_mask);
    if (res > 0) {
        container->flags = NANOCBOR_DECODER_FLAG_CONTAINER;
        container->cur = it->cur + res;
    }
    return res;
}

int nanocbor_enter_array(nanocbor_value_t *it, nanocbor_value_t *array)
{
    return _enter_container(it, array, NANOCBOR_MASK_ARR);
}

int nanocbor_enter_map(nanocbor_value_t *it, nanocbor_value_t *map)
{
    int res = _enter_container(it, map, NANOCBOR_MASK_MAP);

    if (map->remaining > UINT32_MAX / 2) {
        return NANOCBOR_ERR_OVERFLOW;
    }
    map->remaining = map->remaining * 2;
    return res;
}

void nanocbor_leave_container(nanocbor_value_t *it, nanocbor_value_t *container)
{
    it->remaining--;
    if (nanocbor_container_indefinite(container)) {
        it->cur = container->cur + 1;
    }
    else {
        it->cur = container->cur;
    }
}

static int _skip_simple(nanocbor_value_t *it)
{
    uint64_t tmp = 0;
    int res = _get_uint64(it, (uint32_t*)&tmp, NANOCBOR_SIZE_LONG,
                          (uint8_t)nanocbor_get_type(it) << NANOCBOR_TYPE_OFFSET);
    return _advance_if(it, res);
}

int nanocbor_get_subcbor(nanocbor_value_t *it, const uint8_t **start,
                         size_t *len)
{
    *start = it->cur;
    int res = nanocbor_skip(it);
    *len = it->cur - *start;
    return res;
}

int nanocbor_skip_simple(nanocbor_value_t *it)
{
    return _skip_simple(it);
}

int _skip_limited(nanocbor_value_t *it, uint8_t limit)
{
    if (limit == 0) {
        return NANOCBOR_ERR_RECURSION;
    }
    int type = nanocbor_get_type(it);
    int res = type;

    if (type == NANOCBOR_TYPE_BSTR || type == NANOCBOR_TYPE_TSTR) {
        const uint8_t *tmp = NULL;
        size_t len = 0;
        res = _get_str(it, &tmp, &len, type);
    }
    /* map or array */
    else if (type == NANOCBOR_TYPE_ARR || type == NANOCBOR_TYPE_MAP) {
        nanocbor_value_t recurse;
        res = (type == NANOCBOR_TYPE_MAP
               ? nanocbor_enter_map(it, &recurse)
               : nanocbor_enter_array(it, &recurse));
        if (res > 0) {
            while (!nanocbor_at_end(&recurse)) {
                res = _skip_limited(&recurse, limit - 1);
                if (res < 0) {
                    break;
                }
            }
            nanocbor_leave_container(it, &recurse);
        }
    }
    else if (type >= 0) {
        res = _skip_simple(it);
    }
    return res;
}

int nanocbor_skip(nanocbor_value_t *it)
{
    return _skip_limited(it, NANOCBOR_RECURSION_MAX);
}
