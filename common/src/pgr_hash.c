/* Copyright (C) 2026 Ian Curington, Thames 3D Research

    prc_glb_rw is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or (at
    your option) any later version.

    prc_glb_rw is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
    General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with prc_glb_rw. If not, see <https://www.gnu.org/licenses/>.
*/

#include "pgr_hash.h"
#include "pgr_message.h"

#include <stdlib.h>
#include <string.h>

/* Open addressing with linear probing. slots[i] holds a vertex index plus
   one, or 0 for empty; the table is a power of two and at most half full. */
struct pgr_vertex_map
{
    uint32_t stride;
    uint32_t count;
    uint32_t capacity;       /* vertices the data array can hold */
    float *data;
    uint32_t *slots;
    uint32_t mask;           /* slot count - 1 */
};

/* -0.0 becomes 0.0 and any NaN becomes one NaN, so equal-comparing values
   have equal bits. */
static float
canonical(float f)
{
    if (f != f)
    {
        uint32_t bits = 0x7fc00000u;
        float nan;

        memcpy(&nan, &bits, sizeof(nan));
        return nan;
    }
    return f == 0.0f ? 0.0f : f;
}

/* FNV-1a over the canonical bits. */
static uint32_t
hash_vertex(const float *v, uint32_t stride)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    int b;

    for (i = 0; i < stride; i++)
    {
        uint32_t bits;

        memcpy(&bits, &v[i], sizeof(bits));
        for (b = 0; b < 4; b++)
        {
            h ^= (bits >> (8 * b)) & 0xffu;
            h *= 16777619u;
        }
    }
    return h;
}

static int
rehash(pgr_vertex_map *map, uint32_t slot_count)
{
    uint32_t *slots = (uint32_t *)calloc(slot_count, sizeof(uint32_t));
    uint32_t i;

    if (slots == NULL)
        return PGR_ERR_NOMEM;
    for (i = 0; i < map->count; i++)
    {
        uint32_t s = hash_vertex(map->data + (size_t)i * map->stride, map->stride) & (slot_count - 1);

        while (slots[s] != 0)
            s = (s + 1) & (slot_count - 1);
        slots[s] = i + 1;
    }
    free(map->slots);
    map->slots = slots;
    map->mask = slot_count - 1;
    return PGR_OK;
}

pgr_vertex_map *
pgr_vertex_map_new(uint32_t stride, uint32_t expected)
{
    pgr_vertex_map *map;
    uint32_t slots = 16;

    if (stride == 0)
        return NULL;
    map = (pgr_vertex_map *)calloc(1, sizeof(*map));
    if (map == NULL)
        return NULL;
    map->stride = stride;
    while (slots < 0x40000000u && slots / 2 < expected)
        slots *= 2;
    map->capacity = slots / 2;
    map->data = (float *)malloc((size_t)map->capacity * stride * sizeof(float));
    if (map->data == NULL || rehash(map, slots) != PGR_OK)
    {
        pgr_vertex_map_free(map);
        return NULL;
    }
    return map;
}

void
pgr_vertex_map_free(pgr_vertex_map *map)
{
    if (map == NULL)
        return;
    free(map->data);
    free(map->slots);
    free(map);
}

int
pgr_vertex_map_insert(pgr_vertex_map *map, const float *v, uint32_t *index)
{
    float key[64];
    float *heap_key = NULL;
    float *k = key;
    uint32_t i, s;
    int result;

    if (map->stride > 64)
    {
        heap_key = (float *)malloc(map->stride * sizeof(float));
        if (heap_key == NULL)
            return PGR_ERR_NOMEM;
        k = heap_key;
    }
    for (i = 0; i < map->stride; i++)
        k[i] = canonical(v[i]);

    s = hash_vertex(k, map->stride) & map->mask;
    while (map->slots[s] != 0)
    {
        uint32_t found = map->slots[s] - 1;

        if (memcmp(map->data + (size_t)found * map->stride, k, map->stride * sizeof(float)) == 0)
        {
            *index = found;
            free(heap_key);
            return 0;
        }
        s = (s + 1) & map->mask;
    }

    /* New: keep the table at most half full, and the data array large
       enough, before adding. */
    result = 1;
    if (map->count == 0xffffffffu)
        result = PGR_ERR_NOMEM;
    else if (map->count + 1 > (map->mask + 1) / 2)
    {
        if (map->mask + 1 >= 0x80000000u || rehash(map, (map->mask + 1) * 2) != PGR_OK)
            result = PGR_ERR_NOMEM;
    }
    if (result == 1 && map->count >= map->capacity)
    {
        /* The table's limit keeps capacity far below 2^31, but the doubling
           is guarded anyway, so it can never wrap to 0. */
        uint32_t cap = map->capacity >= 0x80000000u ? 0 : map->capacity * 2;
        float *bigger = cap == 0 || map->stride == 0 ? NULL
            : (float *)realloc(map->data, (size_t)cap * map->stride * sizeof(float));

        if (bigger == NULL)
            result = PGR_ERR_NOMEM;
        else
        {
            map->data = bigger;
            map->capacity = cap;
        }
    }
    if (result == 1)
    {
        memcpy(map->data + (size_t)map->count * map->stride, k, map->stride * sizeof(float));
        /* The table may have been rebuilt, so probe again for a free slot. */
        s = hash_vertex(k, map->stride) & map->mask;
        while (map->slots[s] != 0)
            s = (s + 1) & map->mask;
        map->slots[s] = map->count + 1;
        *index = map->count++;
    }
    free(heap_key);
    return result;
}

uint32_t
pgr_vertex_map_count(const pgr_vertex_map *map)
{
    return map->count;
}

const float *
pgr_vertex_map_data(const pgr_vertex_map *map)
{
    return map->data;
}
