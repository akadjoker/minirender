#!/usr/bin/env python3
import argparse
import math
import re
import struct
from collections import defaultdict
from pathlib import Path


MAGIC = 0x4D524C45  # "ELRM"
VERSION = 7

PRIMITIVE_IMPORTED = 19
UV_MESH = 4

ENTITY_PLAYER_START = 0
ENTITY_LIGHT = 1
ENTITY_DOOR = 2
ENTITY_ELEVATOR = 3
ENTITY_PLATFORM = 4
ENTITY_PLACEMENT = 5
ENTITY_TRIGGER = 6

LIGHT_POINT = 0
DOOR_SLIDE = 0


def write_u8(f, v): f.write(struct.pack("<B", int(v) & 0xFF))
def write_u32(f, v): f.write(struct.pack("<I", int(v) & 0xFFFFFFFF))
def write_i32(f, v): f.write(struct.pack("<i", int(v)))
def write_f32(f, v): f.write(struct.pack("<f", float(v)))


def write_str(f, value):
    f.write(str(value).encode("utf-8"))
    f.write(b"\0")


def write_vec2(f, value):
    write_f32(f, value[0])
    write_f32(f, value[1])


def write_vec3(f, value):
    write_f32(f, value[0])
    write_f32(f, value[1])
    write_f32(f, value[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def length(v):
    return math.sqrt(dot(v, v))


def normalize(v):
    l = length(v)
    if l <= 1.0e-6:
        return (0.0, 1.0, 0.0)
    return (v[0] / l, v[1] / l, v[2] / l)


def face_normal(points):
    if len(points) < 3:
        return (0.0, 1.0, 0.0)
    return normalize(cross(sub(points[1], points[0]), sub(points[2], points[0])))


def dominant_uv(pos, normal, scale, shift, rotate_deg):
    ax = tuple(abs(x) for x in normal)
    if ax[1] >= ax[0] and ax[1] >= ax[2]:
        u, v = pos[0], pos[2]
    elif ax[0] >= ax[1] and ax[0] >= ax[2]:
        u, v = pos[2], pos[1]
    else:
        u, v = pos[0], pos[1]

    sx = scale[0] if abs(scale[0]) > 1.0e-6 else 1.0
    sy = scale[1] if abs(scale[1]) > 1.0e-6 else 1.0
    u = u / (64.0 * sx) + shift[0] / 64.0
    v = v / (64.0 * sy) + shift[1] / 64.0

    if rotate_deg:
        r = math.radians(rotate_deg)
        c, s = math.cos(r), math.sin(r)
        u, v = u * c - v * s, u * s + v * c
    return (u, v)


def parse_vec3_text(value):
    nums = [float(x) for x in re.findall(r"[-+]?\d+(?:\.\d+)?", value)]
    if len(nums) < 3:
        return (0.0, 0.0, 0.0)
    return (nums[0], nums[1], nums[2])


def parse_3dt(path):
    lines = Path(path).read_text(errors="replace").splitlines()
    brushes = []
    entities = []
    models = {}

    current_brush = None
    current_face = None
    current_entity = None
    current_model = None
    in_brushes = True
    i = 0
    while i < len(lines):
        raw = lines[i]
        line = raw.strip()

        if line == "Class CEntList":
            in_brushes = False
            if current_brush:
                brushes.append(current_brush)
                current_brush = None
            i += 1
            continue

        if line.startswith("Brush ") and in_brushes:
            if current_face and current_brush:
                current_brush["faces"].append(current_face)
                current_face = None
            if current_brush:
                brushes.append(current_brush)
            name = re.search(r'"(.*)"', line)
            current_brush = {
                "name": name.group(1) if name else "brush",
                "model_id": 0,
                "faces": [],
            }
            i += 1
            continue

        if current_brush and in_brushes:
            if line.startswith("ModelId "):
                current_brush["model_id"] = int(line.split()[1])
            elif line.startswith("NumPoints "):
                if current_face:
                    current_brush["faces"].append(current_face)
                count = int(line.split()[1])
                current_face = {
                    "points": [],
                    "texture": current_brush["name"],
                    "rotate": 0.0,
                    "shift": (0.0, 0.0),
                    "scale": (1.0, 1.0),
                }
                for _ in range(count):
                    i += 1
                    p = lines[i].strip()
                    while not p.startswith("Vec3d ") and i + 1 < len(lines):
                        i += 1
                        p = lines[i].strip()
                    nums = [float(x) for x in p.split()[1:4]]
                    current_face["points"].append((nums[0], nums[1], nums[2]))
            elif line.startswith("TexInfo ") and current_face:
                m = re.search(
                    r'Rotate\s+([-+]?\d+(?:\.\d+)?)\s+Shift\s+([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s+Scale\s+([-+]?\d+(?:\.\d+)?)\s+([-+]?\d+(?:\.\d+)?)\s+Name\s+"([^"]*)"',
                    line,
                )
                if m:
                    current_face["rotate"] = float(m.group(1))
                    current_face["shift"] = (float(m.group(2)), float(m.group(3)))
                    current_face["scale"] = (float(m.group(4)), float(m.group(5)))
                    current_face["texture"] = m.group(6)
            i += 1
            continue

        if line == "CEntity":
            if current_entity:
                entities.append(current_entity)
            current_entity = {"pairs": {}, "origin": (0.0, 0.0, 0.0)}
            i += 1
            continue

        if current_entity:
            if line.startswith("eOrigin "):
                nums = [float(x) for x in line.split()[1:4]]
                current_entity["origin"] = (nums[0], nums[1], nums[2])
            elif line.startswith("Key "):
                m = re.match(r'Key\s+(\S+)\s+Value\s+"(.*)"', line)
                if m:
                    current_entity["pairs"][m.group(1)] = m.group(2)
            elif line == "End CEntity":
                entities.append(current_entity)
                current_entity = None
            i += 1
            continue

        if line.startswith("Model "):
            m = re.search(r'"(.*)"', line)
            current_model = {"name": m.group(1) if m else "model", "id": -1, "origin": (0.0, 0.0, 0.0), "translation_keys": []}
            i += 1
            continue

        if current_model:
            if line.startswith("ModelId "):
                current_model["id"] = int(line.split()[1])
            elif line == "Transform" and i + 1 < len(lines):
                nums = [float(x) for x in lines[i + 1].strip().split()]
                if len(nums) >= 12:
                    current_model["origin"] = (nums[9], nums[10], nums[11])
                i += 1
            elif line.startswith("Translation ") and i + 2 < len(lines):
                keys_line = lines[i + 1].strip()
                if keys_line.startswith("Keys "):
                    parts = keys_line.split()
                    key_count = int(parts[1]) if len(parts) > 1 else 0
                    current_model["translation_keys"] = []
                    key_start = i + 2
                    for key_index in range(key_count):
                        if key_start + key_index >= len(lines):
                            break
                        nums = [float(x) for x in re.findall(r"[-+]?\d+(?:\.\d+)?", lines[key_start + key_index])]
                        if len(nums) >= 4:
                            current_model["translation_keys"].append((nums[0], (nums[1], nums[2], nums[3])))
                    i = key_start + key_count - 1
            elif line.startswith("Model ") or line.startswith("Group ") or line == "Sky":
                if current_model["id"] >= 0:
                    models[current_model["id"]] = current_model
                current_model = None
                continue
            i += 1
            continue

        i += 1

    if current_face and current_brush:
        current_brush["faces"].append(current_face)
    if current_brush:
        brushes.append(current_brush)
    if current_entity:
        entities.append(current_entity)
    if current_model and current_model["id"] >= 0:
        models[current_model["id"]] = current_model

    return brushes, entities, models


def build_objects(brushes, models, texture_dir):
    grouped = defaultdict(list)
    for brush in brushes:
        grouped[brush["model_id"]].append(brush)

    objects = []
    model_id_to_object = {}
    for model_id in sorted(grouped.keys()):
        vertices = []
        faces = []
        for brush in grouped[model_id]:
            for src_face in brush["faces"]:
                points = src_face["points"]
                if len(points) < 3:
                    continue
                n = face_normal(points)
                indices = []
                for p in points:
                    uv = dominant_uv(p, n, src_face["scale"], src_face["shift"], src_face["rotate"])
                    indices.append(len(vertices))
                    vertices.append({"pos": p, "normal": n, "uv": uv})
                indices.reverse()
                n = (-n[0], -n[1], -n[2])
                for idx in indices:
                    vertices[idx]["normal"] = n
                tex = src_face["texture"] or brush["name"] or "default"
                material = "default"
                if tex and tex.lower() != "default":
                    material = f"{tex}.png"
                faces.append({
                    "indices": indices,
                    "material": material,
                    "uv_offset": (0.0, 0.0),
                    "uv_scale": (1.0, 1.0),
                    "uv_rotation": 0.0,
                    "uv_projection": UV_MESH,
                })

        if not vertices or not faces:
            continue

        if model_id == 0:
            name = "world"
            pivot = (0.0, 0.0, 0.0)
        else:
            model = models.get(model_id)
            name = model["name"] if model else f"model_{model_id}"
            pivot = model["origin"] if model else center_of_vertices(vertices)

        model_id_to_object[model_id] = len(objects)
        objects.append({
            "name": name,
            "primitive": PRIMITIVE_IMPORTED,
            "position": (0.0, 0.0, 0.0),
            "rotation": (0.0, 0.0, 0.0),
            "scale": (1.0, 1.0, 1.0),
            "pivot": pivot,
            "visible": True,
            "locked": False,
            "blend": False,
            "two_sided": False,
            "blend_mode": 0,
            "vertices": vertices,
            "faces": faces,
        })

    return objects, model_id_to_object


def center_of_vertices(vertices):
    if not vertices:
        return (0.0, 0.0, 0.0)
    c = [0.0, 0.0, 0.0]
    for v in vertices:
        p = v["pos"]
        c[0] += p[0]
        c[1] += p[1]
        c[2] += p[2]
    inv = 1.0 / len(vertices)
    return (c[0] * inv, c[1] * inv, c[2] * inv)


def convert_entities(src_entities, model_name_to_object, model_name_to_model):
    out = []
    for src in src_entities:
        pairs = src["pairs"]
        classname = pairs.get("classname", "")
        name = pairs.get("%name%", classname or "entity")
        origin = parse_vec3_text(pairs.get("Origin", pairs.get("origin", ""))) if ("Origin" in pairs or "origin" in pairs) else src["origin"]

        ent = {
            "name": name,
            "type": ENTITY_PLACEMENT,
            "position": origin,
            "light_type": LIGHT_POINT,
            "color": (1.0, 1.0, 1.0),
            "intensity": 1.0,
            "radius": 512.0,
            "direction": (0.0, -1.0, 0.0),
            "spot_angle": 45.0,
            "spot_softness": 0.1,
            "door_type": DOOR_SLIDE,
            "door_distance": 128.0,
            "door_speed": 64.0,
            "door_start_open": False,
            "linked_mesh": -1,
            "end_position": (0.0, 128.0, 0.0),
            "move_speed": 64.0,
            "wait_time": 2.0,
            "item_type": 0,
            "rotation_y": 0.0,
            "trigger_radius": 64.0,
            "target_name": pairs.get("Target", pairs.get("target", "")),
            "teleport_target": (0.0, 0.0, 0.0),
            "sound_path": "",
            "sound_radius": 256.0,
            "sound_volume": 1.0,
            "sound_looping": True,
        }

        cls = classname.lower()
        model_name = pairs.get("Model", "")
        model_motion_delta = None
        if model_name in model_name_to_object:
            ent["linked_mesh"] = model_name_to_object[model_name]
        if model_name in model_name_to_model:
            keys = model_name_to_model[model_name].get("translation_keys", [])
            if keys:
                first = keys[0][1]
                farthest = max(keys, key=lambda item: dot(sub(item[1], first), sub(item[1], first)))[1]
                model_motion_delta = sub(farthest, first)

        if cls in ("playerstart", "deathmatchstart", "botmatchstart"):
            ent["type"] = ENTITY_PLAYER_START
            ent["name"] = name or "PlayerStart"
        elif cls in ("light", "dynamiclight", "foglight", "spotlight"):
            ent["type"] = ENTITY_LIGHT
            ent["name"] = name or "Light"
            ent["radius"] = float(pairs.get("Radius", pairs.get("radius", 512.0)))
            ent["intensity"] = float(pairs.get("Intensity", pairs.get("intensity", 1.0)))
            if "Color" in pairs:
                c = parse_vec3_text(pairs["Color"])
                ent["color"] = tuple(max(0.0, min(1.0, x / 255.0 if x > 1.0 else x)) for x in c)
        elif cls == "door":
            ent["type"] = ENTITY_DOOR
            if model_motion_delta and length(model_motion_delta) > 1.0e-4:
                ent["direction"] = normalize(model_motion_delta)
                ent["door_distance"] = length(model_motion_delta)
            else:
                ent["door_distance"] = 128.0
            ent["door_speed"] = 96.0
        elif cls == "movingplat":
            ent["type"] = ENTITY_PLATFORM
            if model_motion_delta:
                ent["end_position"] = (origin[0] + model_motion_delta[0], origin[1] + model_motion_delta[1], origin[2] + model_motion_delta[2])
            ent["move_speed"] = 64.0
        elif "trigger" in cls or cls == "changelevel":
            ent["type"] = ENTITY_TRIGGER
        elif model_name and ent["linked_mesh"] >= 0:
            ent["type"] = ENTITY_PLATFORM
            if model_motion_delta:
                ent["end_position"] = (origin[0] + model_motion_delta[0], origin[1] + model_motion_delta[1], origin[2] + model_motion_delta[2])

        if ent["type"] != ENTITY_PLACEMENT or cls in ("itemhealth", "itemarmor", "itemrocket", "itemgrenade", "itemshredder"):
            out.append(ent)

    return out


def write_scene(path, asset_root, objects, entities):
    with open(path, "wb") as f:
        write_u32(f, MAGIC)
        write_u32(f, VERSION)
        write_str(f, asset_root)
        write_str(f, "")
        write_i32(f, 0)
        write_vec3(f, (0.0, 0.0, 0.0))
        write_vec3(f, (0.0, 0.0, 0.0))
        write_u32(f, 0)  # lightmap UV mesh count

        write_u32(f, len(objects))
        for obj in objects:
            write_str(f, obj["name"])
            write_u32(f, obj["primitive"])
            write_vec3(f, obj["position"])
            write_vec3(f, obj["rotation"])
            write_vec3(f, obj["scale"])
            write_vec3(f, obj["pivot"])
            write_u8(f, 1 if obj["visible"] else 0)
            write_u8(f, 1 if obj["locked"] else 0)
            write_u8(f, 1 if obj["blend"] else 0)
            write_u8(f, 1 if obj["two_sided"] else 0)
            write_u32(f, obj["blend_mode"])

            write_u32(f, len(obj["vertices"]))
            for v in obj["vertices"]:
                write_vec3(f, v["pos"])
                write_vec3(f, v["normal"])
                write_vec2(f, v["uv"])

            write_u32(f, len(obj["faces"]))
            for face in obj["faces"]:
                write_str(f, face["material"])
                write_vec2(f, face["uv_offset"])
                write_vec2(f, face["uv_scale"])
                write_f32(f, face["uv_rotation"])
                write_u32(f, face["uv_projection"])
                write_u32(f, len(face["indices"]))
                for idx in face["indices"]:
                    write_i32(f, idx)

            write_u32(f, 0)  # terrain layers

        write_u32(f, len(entities))
        for ent in entities:
            write_str(f, ent["name"])
            write_u32(f, ent["type"])
            write_vec3(f, ent["position"])
            write_u32(f, ent["light_type"])
            write_vec3(f, ent["color"])
            write_f32(f, ent["intensity"])
            write_f32(f, ent["radius"])
            write_vec3(f, ent["direction"])
            write_f32(f, ent["spot_angle"])
            write_f32(f, ent["spot_softness"])
            write_u32(f, ent["door_type"])
            write_f32(f, ent["door_distance"])
            write_f32(f, ent["door_speed"])
            write_u8(f, 1 if ent["door_start_open"] else 0)
            write_i32(f, ent["linked_mesh"])
            write_vec3(f, ent["end_position"])
            write_f32(f, ent["move_speed"])
            write_f32(f, ent["wait_time"])
            write_i32(f, ent["item_type"])
            write_f32(f, ent["rotation_y"])
            write_f32(f, ent["trigger_radius"])
            write_str(f, ent["target_name"])
            write_vec3(f, ent["teleport_target"])
            write_str(f, ent["sound_path"])
            write_f32(f, ent["sound_radius"])
            write_f32(f, ent["sound_volume"])
            write_u8(f, 1 if ent["sound_looping"] else 0)


def main():
    parser = argparse.ArgumentParser(description="Convert a Genesis3D .3dt text map into MiniRender .mredb.")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--texture-dir", default="", help="Asset root used by the editor to resolve texture PNGs.")
    args = parser.parse_args()

    brushes, src_entities, models = parse_3dt(args.input)
    objects, _ = build_objects(brushes, models, args.texture_dir)
    model_name_to_object = {obj["name"]: i for i, obj in enumerate(objects)}
    model_name_to_model = {model["name"]: model for model in models.values()}
    entities = convert_entities(src_entities, model_name_to_object, model_name_to_model)
    write_scene(args.output, args.texture_dir or "assets", objects, entities)

    face_count = sum(len(obj["faces"]) for obj in objects)
    vert_count = sum(len(obj["vertices"]) for obj in objects)
    print(
        f"Converted {len(brushes)} brushes, {len(objects)} mesh objects, "
        f"{face_count} faces, {vert_count} verts, {len(entities)} entities -> {args.output}"
    )


if __name__ == "__main__":
    main()
