#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path

from importlib.machinery import SourceFileLoader


_mredb = SourceFileLoader("mredb3dt", str(Path(__file__).with_name("3dt_to_mredb.py"))).load_module()


GBSP_CHUNK_MODELS = 1
GBSP_CHUNK_FACES = 11
GBSP_CHUNK_VERT_INDEX = 13
GBSP_CHUNK_VERTS = 14
GBSP_CHUNK_ENTDATA = 16
GBSP_CHUNK_TEXINFO = 17
GBSP_CHUNK_TEXTURES = 18
GBSP_CHUNK_END = 0xFFFF


def read_i32(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def read_u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def read_f32(data, offset):
    return struct.unpack_from("<f", data, offset)[0]


def fixed_string(data, offset, size):
    raw = data[offset:offset + size]
    raw = raw.split(b"\0", 1)[0]
    return raw.decode("utf-8", "replace")


def read_ent_string(data, offset):
    if offset + 4 > len(data):
        return "", len(data)
    size = read_i32(data, offset)
    offset += 4
    if size < 0 or offset + size > len(data):
        return "", len(data)
    raw = data[offset:offset + size]
    offset += size
    if raw.endswith(b"\0"):
        raw = raw[:-1]
    return raw.decode("utf-8", "replace"), offset


def parse_entdata(data):
    if len(data) < 4:
        return []
    offset = 0
    count = read_i32(data, offset)
    offset += 4
    entities = []
    if count < 0 or count > 100000:
        return entities
    for _ in range(count):
        if offset + 4 > len(data):
            break
        pair_count = read_i32(data, offset)
        offset += 4
        if pair_count < 0 or pair_count > 10000:
            break
        ent = {"pairs": {}, "origin": (0.0, 0.0, 0.0)}
        for _ in range(pair_count):
            key, offset = read_ent_string(data, offset)
            value, offset = read_ent_string(data, offset)
            ent["pairs"][key] = value
            if key.lower() == "origin":
                ent["origin"] = _mredb.parse_vec3_text(value)
        entities.append(ent)
    return entities


def parse_gbsp(path):
    data = Path(path).read_bytes()
    offset = 0
    models = []
    faces = []
    vert_indices = []
    verts = []
    texinfos = []
    textures = []
    entdata = b""

    while offset + 12 <= len(data):
        chunk_type, elem_size, elem_count = struct.unpack_from("<iii", data, offset)
        offset += 12
        if chunk_type == GBSP_CHUNK_END:
            break
        if elem_size < 0 or elem_count < 0:
            raise RuntimeError("negative chunk size/count")
        chunk_bytes = elem_size * elem_count
        chunk_start = offset
        chunk_end = offset + chunk_bytes
        if chunk_end > len(data):
            raise RuntimeError("chunk extends past end of file")

        if chunk_type == GBSP_CHUNK_MODELS:
            for i in range(elem_count):
                o = chunk_start + i * elem_size
                models.append({
                    "name": "world" if i == 0 else f"model_{i}",
                    "id": i,
                    "origin": (read_f32(data, o + 32), read_f32(data, o + 36), read_f32(data, o + 40)),
                    "first_face": read_i32(data, o + 44),
                    "num_faces": read_i32(data, o + 48),
                    "translation_keys": [],
                })
        elif chunk_type == GBSP_CHUNK_FACES:
            for i in range(elem_count):
                o = chunk_start + i * elem_size
                faces.append({
                    "first_vert": read_i32(data, o + 0),
                    "num_verts": read_i32(data, o + 4),
                    "plane_side": read_i32(data, o + 12),
                    "texinfo": read_i32(data, o + 16),
                })
        elif chunk_type == GBSP_CHUNK_VERT_INDEX:
            vert_indices = [read_i32(data, chunk_start + i * 4) for i in range(elem_count)]
        elif chunk_type == GBSP_CHUNK_VERTS:
            for i in range(elem_count):
                o = chunk_start + i * elem_size
                verts.append((read_f32(data, o + 0), read_f32(data, o + 4), read_f32(data, o + 8)))
        elif chunk_type == GBSP_CHUNK_TEXINFO:
            for i in range(elem_count):
                o = chunk_start + i * elem_size
                texinfos.append({
                    "vec0": (read_f32(data, o + 0), read_f32(data, o + 4), read_f32(data, o + 8)),
                    "vec1": (read_f32(data, o + 12), read_f32(data, o + 16), read_f32(data, o + 20)),
                    "shift": (read_f32(data, o + 24), read_f32(data, o + 28)),
                    "draw_scale": (read_f32(data, o + 32), read_f32(data, o + 36)),
                    "texture": read_i32(data, o + 60),
                })
        elif chunk_type == GBSP_CHUNK_TEXTURES:
            for i in range(elem_count):
                o = chunk_start + i * elem_size
                textures.append({
                    "name": fixed_string(data, o, 32),
                    "width": read_i32(data, o + 36),
                    "height": read_i32(data, o + 40),
                })
        elif chunk_type == GBSP_CHUNK_ENTDATA:
            entdata = data[chunk_start:chunk_end]

        offset = chunk_end

    return {
        "models": models,
        "faces": faces,
        "vert_indices": vert_indices,
        "verts": verts,
        "texinfos": texinfos,
        "textures": textures,
        "entities": parse_entdata(entdata),
    }


def texture_name_for_face(gbsp, face):
    ti = face["texinfo"]
    if 0 <= ti < len(gbsp["texinfos"]):
        tex_index = gbsp["texinfos"][ti]["texture"]
        if 0 <= tex_index < len(gbsp["textures"]):
            return gbsp["textures"][tex_index]["name"] or "default"
    return "default"


def uv_for_vertex(gbsp, face, pos):
    ti = face["texinfo"]
    if not (0 <= ti < len(gbsp["texinfos"])):
        return (0.0, 0.0)
    texinfo = gbsp["texinfos"][ti]
    tex_index = texinfo["texture"]
    tex_w = 64
    tex_h = 64
    if 0 <= tex_index < len(gbsp["textures"]):
        tex_w = max(1, gbsp["textures"][tex_index]["width"])
        tex_h = max(1, gbsp["textures"][tex_index]["height"])
    scale_u = texinfo["draw_scale"][0] if abs(texinfo["draw_scale"][0]) > 1.0e-6 else 1.0
    scale_v = texinfo["draw_scale"][1] if abs(texinfo["draw_scale"][1]) > 1.0e-6 else 1.0
    u = (_mredb.dot(pos, texinfo["vec0"]) * scale_u + texinfo["shift"][0]) / tex_w
    v = (_mredb.dot(pos, texinfo["vec1"]) * scale_v + texinfo["shift"][1]) / tex_h
    return (u, v)


def face_uv_adjust(gbsp, face, points):
    ti = face["texinfo"]
    if not points or not (0 <= ti < len(gbsp["texinfos"])):
        return (0.0, 0.0)
    texinfo = gbsp["texinfos"][ti]
    tex_index = texinfo["texture"]
    if not (0 <= tex_index < len(gbsp["textures"])):
        return (texinfo["shift"][0], texinfo["shift"][1])

    tex_w = max(1, gbsp["textures"][tex_index]["width"])
    tex_h = max(1, gbsp["textures"][tex_index]["height"])
    min_u = min(_mredb.dot(p, texinfo["vec0"]) for p in points)
    min_v = min(_mredb.dot(p, texinfo["vec1"]) for p in points)

    # Match genesis_converter: snap the face-local texture origin to a whole texture tile.
    adjust_scale_u = (1.0 / texinfo["draw_scale"][0]) if abs(texinfo["draw_scale"][0]) > 1.0e-6 else 1.0
    adjust_scale_v = (1.0 / texinfo["draw_scale"][1]) if abs(texinfo["draw_scale"][1]) > 1.0e-6 else 1.0
    au = int((min_u * adjust_scale_u + texinfo["shift"][0]) / tex_w) * tex_w
    av = int((min_v * adjust_scale_v + texinfo["shift"][1]) / tex_h) * tex_h
    return (texinfo["shift"][0] - au, texinfo["shift"][1] - av)


def uv_for_vertex_adjusted(gbsp, face, pos, adjust):
    ti = face["texinfo"]
    if not (0 <= ti < len(gbsp["texinfos"])):
        return (0.0, 0.0)
    texinfo = gbsp["texinfos"][ti]
    tex_index = texinfo["texture"]
    tex_w = 64
    tex_h = 64
    if 0 <= tex_index < len(gbsp["textures"]):
        tex_w = max(1, gbsp["textures"][tex_index]["width"])
        tex_h = max(1, gbsp["textures"][tex_index]["height"])
    scale_u = texinfo["draw_scale"][0] if abs(texinfo["draw_scale"][0]) > 1.0e-6 else 1.0
    scale_v = texinfo["draw_scale"][1] if abs(texinfo["draw_scale"][1]) > 1.0e-6 else 1.0
    u = (_mredb.dot(pos, texinfo["vec0"]) * scale_u + adjust[0]) / tex_w
    v = (_mredb.dot(pos, texinfo["vec1"]) * scale_v + adjust[1]) / tex_h
    return (u, v)


def build_objects(gbsp, texture_dir, flip_winding):
    objects = []
    model_id_to_object = {}
    if not gbsp["models"]:
        gbsp["models"].append({"name": "world", "id": 0, "origin": (0.0, 0.0, 0.0), "first_face": 0, "num_faces": len(gbsp["faces"])})

    for model in gbsp["models"]:
        vertices = []
        out_faces = []
        first = model["first_face"]
        last = first + model["num_faces"]
        for face_index in range(first, min(last, len(gbsp["faces"]))):
            face = gbsp["faces"][face_index]
            if face["num_verts"] < 3:
                continue
            if face["first_vert"] < 0 or face["first_vert"] + face["num_verts"] > len(gbsp["vert_indices"]):
                continue
            points = []
            for i in range(face["num_verts"]):
                src = gbsp["vert_indices"][face["first_vert"] + i]
                if 0 <= src < len(gbsp["verts"]):
                    points.append(gbsp["verts"][src])
            if len(points) < 3:
                continue
            n = _mredb.face_normal(points)
            if face["plane_side"]:
                n = (-n[0], -n[1], -n[2])
            uv_adjust = face_uv_adjust(gbsp, face, points)

            indices = []
            for p in points:
                indices.append(len(vertices))
                vertices.append({"pos": p, "normal": n, "uv": uv_for_vertex_adjusted(gbsp, face, p, uv_adjust)})
            if flip_winding:
                indices.reverse()
                n = (-n[0], -n[1], -n[2])
                for idx in indices:
                    vertices[idx]["normal"] = n

            tex = texture_name_for_face(gbsp, face)
            out_faces.append({
                "indices": indices,
                "material": f"{tex}.png" if tex and tex != "default" else "default",
                "uv_offset": (0.0, 0.0),
                "uv_scale": (1.0, 1.0),
                "uv_rotation": 0.0,
                "uv_projection": _mredb.UV_MESH,
            })

        if not vertices or not out_faces:
            continue
        model_id_to_object[model["id"]] = len(objects)
        objects.append({
            "name": model["name"],
            "primitive": _mredb.PRIMITIVE_IMPORTED,
            "position": (0.0, 0.0, 0.0),
            "rotation": (0.0, 0.0, 0.0),
            "scale": (1.0, 1.0, 1.0),
            "pivot": model["origin"] if model["id"] != 0 else (0.0, 0.0, 0.0),
            "visible": True,
            "locked": False,
            "blend": False,
            "two_sided": False,
            "blend_mode": 0,
            "vertices": vertices,
            "faces": out_faces,
        })
    return objects, model_id_to_object


def main():
    parser = argparse.ArgumentParser(description="Convert Genesis3D GBSP/BSP final geometry to MiniRender .mredb.")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--texture-dir", default="", help="Asset root used by the editor to resolve texture PNGs.")
    parser.add_argument("--no-flip-winding", action="store_true", help="Keep GBSP face winding as-is.")
    args = parser.parse_args()

    gbsp = parse_gbsp(args.input)
    objects, model_id_to_object = build_objects(gbsp, args.texture_dir, not args.no_flip_winding)

    model_name_to_object = {obj["name"]: i for i, obj in enumerate(objects)}
    # GBSP entdata does not preserve model name mapping reliably, but this keeps generic entities/lights/player starts.
    entities = _mredb.convert_entities(gbsp["entities"], model_name_to_object, {})

    _mredb.write_scene(args.output, args.texture_dir or "assets", objects, entities)
    face_count = sum(len(obj["faces"]) for obj in objects)
    vert_count = sum(len(obj["vertices"]) for obj in objects)
    print(
        f"Converted {len(gbsp['models'])} models, {len(objects)} mesh objects, "
        f"{face_count} faces, {vert_count} verts, {len(entities)} entities -> {args.output}"
    )


if __name__ == "__main__":
    main()
