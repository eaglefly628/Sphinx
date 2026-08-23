"""
UE editor-Python importer for surface_tile.json  (UE 5.3+, tested target 5.6).

Recreates the satellite-ground result inside any blank Unreal project with
zero C++ / zero Cesium:

    ground.glb          -> StaticMesh (1 unit = 1 m -> x100 cm)
    albedo/normal/splat -> Textures (correct sRGB / compression)
    material spec       -> M_SatGround (albedo + normal + splat-driven
                           roughness from the 8 class layers)
    objects[]           -> small sphere markers, correctly georegistered

Frame mapping (glTF Y-up -> UE Z-up handled by the importer):
    UE +X = east, UE +Y = south, UE +Z = up, mesh centred on tile centre.
    Marker position (cm) = ((e - half)*100, -(n - half)*100, z*100).

How to run (Mac or any platform):
    1. Enable plugin "Python Editor Script Plugin" (Edit > Plugins), restart.
    2. Set OUTPUT_DIR below (absolute path to Tools/SatelliteGround/output),
       or set env SATGROUND_OUTPUT before launching the editor.
    3. Output Log > cmd dropdown to "Python", then:
         py "/path/to/ue/import_surface_tile.py"
    4. Assets land in /Game/SatGround; the ground + markers appear in the
       current level (save it after).
"""
import json
import os

import unreal

# --------------------------------------------------------------------------- #
OUTPUT_DIR = os.environ.get("SATGROUND_OUTPUT", "")   # <- EDIT ME if no env
ASSET_PATH = "/Game/SatGround"
SPAWN_IN_LEVEL = True
MARKER_MAX = 200
# --------------------------------------------------------------------------- #

if not OUTPUT_DIR:
    # sensible default: repo checkout next to this script
    OUTPUT_DIR = os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "output"))

TILE = json.load(open(os.path.join(OUTPUT_DIR, "surface_tile.json")))
HALF = TILE["geo"]["size_m"] * 0.5
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def log(msg):
    unreal.log("[SatGround] " + str(msg))


def import_file(filename, dest_name):
    task = unreal.AssetImportTask()
    task.filename = os.path.join(OUTPUT_DIR, filename)
    task.destination_path = ASSET_PATH
    task.destination_name = dest_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    paths = list(task.imported_object_paths or [])
    if not paths:
        raise RuntimeError("import produced no asset: " + filename)
    return unreal.load_asset(paths[0])


def import_texture(filename, dest_name, srgb, normal_map=False, masks=False):
    tex = import_file(filename, dest_name)
    tex.set_editor_property("srgb", srgb)
    if normal_map:
        tex.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("flip_green_channel", False)
    elif masks:
        tex.set_editor_property(
            "compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    EAL.save_loaded_asset(tex)
    return tex


def build_material(t_albedo, t_normal, t_splat0, t_splat1):
    """Albedo + normal, roughness blended from the 8 splat-weighted layers."""
    name = "M_SatGround"
    pkg = ASSET_PATH + "/" + name
    if EAL.does_asset_exist(pkg):
        EAL.delete_asset(pkg)
    mat = AT.create_asset(name, ASSET_PATH, unreal.Material,
                          unreal.MaterialFactoryNew())

    def tex_sample(tex, x, y, sampler=None):
        e = MEL.create_material_expression(
            mat, unreal.MaterialExpressionTextureSample, x, y)
        e.set_editor_property("texture", tex)
        if sampler is not None:
            e.set_editor_property("sampler_type", sampler)
        return e

    s_alb = tex_sample(t_albedo, -700, -300)
    MEL.connect_material_property(s_alb, "RGB",
                                  unreal.MaterialProperty.MP_BASE_COLOR)
    s_nrm = tex_sample(t_normal, -700, 300,
                       unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    MEL.connect_material_property(s_nrm, "RGB",
                                  unreal.MaterialProperty.MP_NORMAL)

    try:
        rough = [layer["roughness"] for layer in TILE["material"]["layers"]]
        s0 = tex_sample(t_splat0, -1100, 0,
                        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        s1 = tex_sample(t_splat1, -1100, 150,
                        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)

        def dot3(sample, consts, x, y):
            c = MEL.create_material_expression(
                mat, unreal.MaterialExpressionConstant3Vector, x, y)
            c.set_editor_property("constant", unreal.LinearColor(*consts, 1.0))
            d = MEL.create_material_expression(
                mat, unreal.MaterialExpressionDotProduct, x + 150, y)
            MEL.connect_material_expressions(sample, "RGB", d, "A")
            MEL.connect_material_expressions(c, "", d, "B")
            return d

        def mul_a(sample, k, x, y):
            c = MEL.create_material_expression(
                mat, unreal.MaterialExpressionConstant, x, y)
            c.set_editor_property("r", k)
            m = MEL.create_material_expression(
                mat, unreal.MaterialExpressionMultiply, x + 150, y)
            MEL.connect_material_expressions(sample, "A", m, "A")
            MEL.connect_material_expressions(c, "", m, "B")
            return m

        terms = [dot3(s0, rough[0:3], -900, -100), mul_a(s0, rough[3], -900, 0),
                 dot3(s1, rough[4:7], -900, 100), mul_a(s1, rough[7], -900, 200)]
        acc = terms[0]
        for i, t in enumerate(terms[1:]):
            a = MEL.create_material_expression(
                mat, unreal.MaterialExpressionAdd, -500 + i * 120, 60)
            MEL.connect_material_expressions(acc, "", a, "A")
            MEL.connect_material_expressions(t, "", a, "B")
            acc = a
        MEL.connect_material_property(acc, "",
                                      unreal.MaterialProperty.MP_ROUGHNESS)
        log("splat-driven roughness wired (8 layers)")
    except Exception as exc:  # material still works without it
        log("roughness graph skipped: %s" % exc)

    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    return mat


def main():
    log("importing from " + OUTPUT_DIR)
    t_alb = import_texture("albedo.png", "T_SatGround_Albedo", srgb=True)
    t_nrm = import_texture("normal.png", "T_SatGround_Normal", srgb=False,
                           normal_map=True)
    t_sp0 = import_texture("splat_0_rgba.png", "T_SatGround_Splat0",
                           srgb=False, masks=True)
    t_sp1 = import_texture("splat_1_rgba.png", "T_SatGround_Splat1",
                           srgb=False, masks=True)
    mesh = import_file("ground.glb", "SM_SatGround")
    if isinstance(mesh, unreal.StaticMesh) is False:
        # Interchange may return a container; find the static mesh
        for p in EAL.list_assets(ASSET_PATH, recursive=True):
            a = unreal.load_asset(p.split(".")[0])
            if isinstance(a, unreal.StaticMesh):
                mesh = a
                break
    mat = build_material(t_alb, t_nrm, t_sp0, t_sp1)
    if isinstance(mesh, unreal.StaticMesh):
        mesh.set_material(0, mat)
        EAL.save_loaded_asset(mesh)

    if SPAWN_IN_LEVEL:
        sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        ground = sub.spawn_actor_from_object(mesh, unreal.Vector(0, 0, 0))
        ground.set_actor_scale3d(unreal.Vector(100, 100, 100))  # m -> cm
        ground.set_actor_label("SatGround_%s" % TILE["tile_id"])
        ground.set_folder_path("SatGround")

        sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
        items = TILE["objects"]["items"][:MARKER_MAX]
        for i, o in enumerate(items):
            x = (o["local_e_m"] - HALF) * 100.0
            y = -(o["local_n_m"] - HALF) * 100.0
            a = sub.spawn_actor_from_object(sphere, unreal.Vector(x, y, 120.0))
            a.set_actor_scale3d(unreal.Vector(0.6, 0.6, 0.6))
            a.set_actor_label("obj_%02d_%s" % (i, o.get("class_key", "object")))
            a.set_folder_path("SatGround/Objects")
        log("spawned ground + %d object markers" % len(items))

    c = TILE["mesh"]
    log("tile centre lon/lat = (%.6f, %.6f)  size = %.0f m" %
        (c["center_lon"], c["center_lat"], TILE["geo"]["size_m"]))
    log("DONE - assets in %s" % ASSET_PATH)


main()
