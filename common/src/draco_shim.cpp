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

/* The C interface of draco_shim.h over Google's Draco (C++, Apache-2.0).

   The only C++ in prc_glb_rw, compiled only with PRC_GLB_WITH_DRACO=ON.
   Every entry point catches every exception: running out of memory is
   DS_ERR_NOMEM, anything else the call's own failure code. What it returns
   is malloc'd, so the C side frees it without new or delete crossing the
   interface. */

#include "draco_shim.h"

#include "draco/compression/decode.h"
#include "draco/compression/encode.h"
#include "draco/core/decoder_buffer.h"
#include "draco/core/draco_version.h"
#include "draco/core/encoder_buffer.h"
#include "draco/mesh/mesh.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace
{

int
semantic_of(draco::GeometryAttribute::Type t)
{
    switch (t)
    {
    case draco::GeometryAttribute::POSITION:
        return DS_POSITION;
    case draco::GeometryAttribute::NORMAL:
        return DS_NORMAL;
    case draco::GeometryAttribute::COLOR:
        return DS_COLOR;
    case draco::GeometryAttribute::TEX_COORD:
        return DS_TEXCOORD;
    default:
        return DS_GENERIC;
    }
}

draco::GeometryAttribute::Type
type_of(int semantic)
{
    switch (semantic)
    {
    case DS_POSITION:
        return draco::GeometryAttribute::POSITION;
    case DS_NORMAL:
        return draco::GeometryAttribute::NORMAL;
    case DS_COLOR:
        return draco::GeometryAttribute::COLOR;
    case DS_TEXCOORD:
        return draco::GeometryAttribute::TEX_COORD;
    default:
        return draco::GeometryAttribute::GENERIC;
    }
}

/* count * size bytes, or NULL when that would wrap or memory runs out. */
void *
alloc_array(size_t count, size_t size)
{
    if (size != 0 && count > (size_t)-1 / size)
        return NULL;
    return std::malloc(count * size ? count * size : 1);
}

int
decode(const void *buf, size_t size, const int *wanted_ids, uint32_t n_wanted, ds_mesh *out)
{
    draco::DecoderBuffer buffer;
    draco::Decoder decoder;

    buffer.Init(static_cast<const char *>(buf), size);
    draco::StatusOr<std::unique_ptr<draco::Mesh> > decoded = decoder.DecodeMeshFromBuffer(&buffer);
    if (!decoded.ok() || decoded.value() == NULL)
        return DS_ERR_DECODE;
    const draco::Mesh &mesh = *decoded.value();

    const uint32_t num_points = mesh.num_points();
    const uint32_t num_faces = mesh.num_faces();

    out->num_points = num_points;
    out->num_faces = num_faces;
    out->indices = static_cast<uint32_t *>(alloc_array((size_t)num_faces * 3, sizeof(uint32_t)));
    out->attributes = static_cast<ds_attribute *>(std::calloc(n_wanted ? n_wanted : 1, sizeof(ds_attribute)));
    if (out->indices == NULL || out->attributes == NULL)
        return DS_ERR_NOMEM;
    out->num_attributes = n_wanted;

    for (uint32_t f = 0; f < num_faces; f++)
    {
        const draco::Mesh::Face &face = mesh.face(draco::FaceIndex(f));

        for (int k = 0; k < 3; k++)
        {
            const uint32_t v = face[k].value();

            if (v >= num_points)
                return DS_ERR_DECODE;
            out->indices[3 * f + k] = v;
        }
    }

    for (uint32_t a = 0; a < n_wanted; a++)
    {
        ds_attribute *dst = &out->attributes[a];
        const draco::PointAttribute *att;
        int nc;

        dst->draco_id = wanted_ids[a];
        if (wanted_ids[a] < 0)
            continue;
        att = mesh.GetAttributeByUniqueId(static_cast<uint32_t>(wanted_ids[a]));
        if (att == NULL)
            continue;
        nc = att->num_components();
        if (nc < 1 || nc > 4)
            return DS_ERR_DECODE;
        dst->semantic = semantic_of(att->attribute_type());
        dst->num_components = nc;
        dst->data = static_cast<float *>(alloc_array((size_t)num_points * (size_t)nc, sizeof(float)));
        if (dst->data == NULL)
            return DS_ERR_NOMEM;
        for (uint32_t p = 0; p < num_points; p++)
        {
            const draco::AttributeValueIndex vi = att->mapped_index(draco::PointIndex(p));

            if (vi.value() >= att->size()
                || !att->ConvertValue<float>(vi, static_cast<int8_t>(nc), dst->data + (size_t)p * nc))
                return DS_ERR_DECODE;
        }
    }
    return DS_OK;
}

int
encode(const ds_mesh *in, const ds_encode_options *o, void **out_buf, size_t *out_size, int *out_ids)
{
    draco::Mesh mesh;
    std::vector<int> att_ids(in->num_attributes);

    mesh.set_num_points(in->num_points);
    for (uint32_t a = 0; a < in->num_attributes; a++)
    {
        const ds_attribute *src = &in->attributes[a];
        std::unique_ptr<draco::PointAttribute> att(new draco::PointAttribute());

        if (src->num_components < 1 || src->num_components > 4 || src->data == NULL)
            return DS_ERR_ARG;
        att->Init(type_of(src->semantic), static_cast<int8_t>(src->num_components), draco::DT_FLOAT32,
                  false, in->num_points);
        const int id = mesh.AddAttribute(std::move(att));
        if (id < 0)
            return DS_ERR_ENCODE;
        draco::PointAttribute *pa = mesh.attribute(id);
        for (uint32_t p = 0; p < in->num_points; p++)
            pa->SetAttributeValue(pa->mapped_index(draco::PointIndex(p)),
                                  src->data + (size_t)p * src->num_components);
        att_ids[a] = id;
    }

    mesh.SetNumFaces(in->num_faces);
    for (uint32_t f = 0; f < in->num_faces; f++)
    {
        draco::Mesh::Face face;

        for (int k = 0; k < 3; k++)
        {
            const uint32_t v = in->indices[3 * f + k];

            if (v >= in->num_points)
                return DS_ERR_ARG;
            face[k] = draco::PointIndex(v);
        }
        mesh.SetFace(draco::FaceIndex(f), face);
    }

    /* As Draco's own encoders do: merge equal values, then points whose
       attributes are all equal. The point count may change; the decoded
       mesh says what it became. */
#ifdef DRACO_ATTRIBUTE_VALUES_DEDUPLICATION_SUPPORTED
    if (!mesh.DeduplicateAttributeValues())
        return DS_ERR_ENCODE;
#endif
#ifdef DRACO_ATTRIBUTE_INDICES_DEDUPLICATION_SUPPORTED
    mesh.DeduplicatePointIds();
#endif

    draco::Encoder encoder;
    encoder.SetSpeedOptions(o->encode_speed, o->decode_speed);
    if (o->qbits_position > 0)
        encoder.SetAttributeQuantization(draco::GeometryAttribute::POSITION, o->qbits_position);
    if (o->qbits_normal > 0)
        encoder.SetAttributeQuantization(draco::GeometryAttribute::NORMAL, o->qbits_normal);
    if (o->qbits_texcoord > 0)
        encoder.SetAttributeQuantization(draco::GeometryAttribute::TEX_COORD, o->qbits_texcoord);
    if (o->qbits_color > 0)
        encoder.SetAttributeQuantization(draco::GeometryAttribute::COLOR, o->qbits_color);
    if (o->qbits_generic > 0)
        encoder.SetAttributeQuantization(draco::GeometryAttribute::GENERIC, o->qbits_generic);

    draco::EncoderBuffer buffer;
    if (!encoder.EncodeMeshToBuffer(mesh, &buffer).ok())
        return DS_ERR_ENCODE;

    void *bytes = std::malloc(buffer.size() ? buffer.size() : 1);
    if (bytes == NULL)
        return DS_ERR_NOMEM;
    std::memcpy(bytes, buffer.data(), buffer.size());
    *out_buf = bytes;
    *out_size = buffer.size();
    if (out_ids != NULL)
        for (uint32_t a = 0; a < in->num_attributes; a++)
            out_ids[a] = static_cast<int>(mesh.attribute(att_ids[a])->unique_id());
    return DS_OK;
}

bool
valid_options(const ds_encode_options *o)
{
    const int q[5] = { o->qbits_position, o->qbits_normal, o->qbits_texcoord, o->qbits_color,
                       o->qbits_generic };

    for (int i = 0; i < 5; i++)
        if (q[i] < 0 || q[i] > 30)
            return false;
    return o->encode_speed >= 0 && o->encode_speed <= 10 && o->decode_speed >= 0
           && o->decode_speed <= 10;
}

} // namespace

extern "C" {

int
ds_available(void)
{
    return 1;
}

const char *
ds_version(void)
{
    return draco::kDracoVersion;
}

int
ds_decode(const void *buf, size_t size, const int *wanted_ids, uint32_t n_wanted, ds_mesh *out)
{
    int st;

    if (out == NULL)
        return DS_ERR_ARG;
    std::memset(out, 0, sizeof(*out));
    if (buf == NULL || size == 0 || (n_wanted > 0 && wanted_ids == NULL))
        return DS_ERR_ARG;
    try
    {
        st = decode(buf, size, wanted_ids, n_wanted, out);
    }
    catch (const std::bad_alloc &)
    {
        st = DS_ERR_NOMEM;
    }
    catch (...)
    {
        st = DS_ERR_DECODE;
    }
    if (st != DS_OK)
        ds_mesh_free(out);
    return st;
}

void
ds_mesh_free(ds_mesh *m)
{
    if (m == NULL)
        return;
    if (m->attributes != NULL)
        for (uint32_t a = 0; a < m->num_attributes; a++)
            std::free(m->attributes[a].data);
    std::free(m->attributes);
    std::free(m->indices);
    std::memset(m, 0, sizeof(*m));
}

void
ds_encode_options_default(ds_encode_options *o)
{
    if (o == NULL)
        return;
    o->qbits_position = 14;
    o->qbits_normal = 10;
    o->qbits_texcoord = 12;
    o->qbits_color = 8;
    o->qbits_generic = 12;
    o->encode_speed = 5;
    o->decode_speed = 5;
}

int
ds_encode(const ds_mesh *in, const ds_encode_options *o, void **out_buf, size_t *out_size, int *out_ids)
{
    ds_encode_options defaults;
    int st;

    if (out_buf != NULL)
        *out_buf = NULL;
    if (out_size != NULL)
        *out_size = 0;
    if (in == NULL || out_buf == NULL || out_size == NULL || in->num_points == 0 || in->num_faces == 0
        || in->indices == NULL || (in->num_attributes > 0 && in->attributes == NULL))
        return DS_ERR_ARG;
    if (o == NULL)
    {
        ds_encode_options_default(&defaults);
        o = &defaults;
    }
    if (!valid_options(o))
        return DS_ERR_ARG;
    try
    {
        st = encode(in, o, out_buf, out_size, out_ids);
    }
    catch (const std::bad_alloc &)
    {
        st = DS_ERR_NOMEM;
    }
    catch (...)
    {
        st = DS_ERR_ENCODE;
    }
    if (st != DS_OK && *out_buf != NULL)
    {
        std::free(*out_buf);
        *out_buf = NULL;
        *out_size = 0;
    }
    return st;
}

void
ds_buffer_free(void *buf)
{
    std::free(buf);
}

} // extern "C"
