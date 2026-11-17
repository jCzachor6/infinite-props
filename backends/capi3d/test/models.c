/* Models through the C boundary: load, reflect, set parameters, generate and
 * read every view, with the values the fixture source implies. A texture is
 * compared with what the 2D API generates from the skin's own program. */
#include "iprop3d.h"
#include "check.h"

/* API 0.1 layouts on LP64, including every public field offset. */
#define LAYOUT(type, size) typedef char layout_##type[(sizeof(type) == (size)) ? 1 : -1]
#define OFFSET(type, field, offset) typedef char offset_##type##_##field[(offsetof(type, field) == (offset)) ? 1 : -1]
LAYOUT(iprop3d_generate_options, 12);
OFFSET(iprop3d_generate_options, flags, 4); OFFSET(iprop3d_generate_options, seed, 8);
LAYOUT(iprop3d_library_info, 40);
OFFSET(iprop3d_library_info, api_major, 4); OFFSET(iprop3d_library_info, api_minor, 8);
OFFSET(iprop3d_library_info, max_vertices, 12); OFFSET(iprop3d_library_info, max_triangles, 16);
OFFSET(iprop3d_library_info, max_textures, 20); OFFSET(iprop3d_library_info, max_texture_side, 24);
OFFSET(iprop3d_library_info, max_texels, 32);
LAYOUT(iprop3d_model_info, 48);
OFFSET(iprop3d_model_info, flags, 4); OFFSET(iprop3d_model_info, size_x, 8); OFFSET(iprop3d_model_info, size_y, 12);
OFFSET(iprop3d_model_info, size_z, 16); OFFSET(iprop3d_model_info, units_per_meter, 20);
OFFSET(iprop3d_model_info, fuel, 24); OFFSET(iprop3d_model_info, input_count, 32);
OFFSET(iprop3d_model_info, skin_program_count, 36); OFFSET(iprop3d_model_info, container_flags, 40);
LAYOUT(iprop3d_result_info, 96);
OFFSET(iprop3d_result_info, flags, 4); OFFSET(iprop3d_result_info, bounds_min, 8);
OFFSET(iprop3d_result_info, bounds_max, 20); OFFSET(iprop3d_result_info, size, 32);
OFFSET(iprop3d_result_info, units_per_meter, 44); OFFSET(iprop3d_result_info, vertex_count, 48);
OFFSET(iprop3d_result_info, triangle_count, 52); OFFSET(iprop3d_result_info, surface_count, 56);
OFFSET(iprop3d_result_info, texture_count, 60); OFFSET(iprop3d_result_info, fuel_budget, 64);
OFFSET(iprop3d_result_info, fuel_used, 72); OFFSET(iprop3d_result_info, work_budget, 80);
OFFSET(iprop3d_result_info, work_used, 88);
#if UINTPTR_MAX > 0xffffffffu
LAYOUT(iprop3d_skin_program_info, 40);
OFFSET(iprop3d_skin_program_info, flags, 4); OFFSET(iprop3d_skin_program_info, program_index, 8);
OFFSET(iprop3d_skin_program_info, width, 12); OFFSET(iprop3d_skin_program_info, height, 16);
OFFSET(iprop3d_skin_program_info, name, 24);
LAYOUT(iprop3d_surface_view, 96);
OFFSET(iprop3d_surface_view, flags, 4); OFFSET(iprop3d_surface_view, surface_index, 8);
OFFSET(iprop3d_surface_view, texture_index, 12); OFFSET(iprop3d_surface_view, roughness, 16);
OFFSET(iprop3d_surface_view, metallic, 20); OFFSET(iprop3d_surface_view, emission, 24);
OFFSET(iprop3d_surface_view, alpha_mode, 28); OFFSET(iprop3d_surface_view, alpha_cutoff, 32);
OFFSET(iprop3d_surface_view, two_sided, 36); OFFSET(iprop3d_surface_view, crisp, 40);
OFFSET(iprop3d_surface_view, vertex_count, 44); OFFSET(iprop3d_surface_view, index_count, 48);
OFFSET(iprop3d_surface_view, positions, 56); OFFSET(iprop3d_surface_view, normals, 64);
OFFSET(iprop3d_surface_view, tangents, 72); OFFSET(iprop3d_surface_view, uvs, 80);
OFFSET(iprop3d_surface_view, indices, 88);
LAYOUT(iprop3d_texture_view, 96);
OFFSET(iprop3d_texture_view, flags, 4); OFFSET(iprop3d_texture_view, texture_index, 8);
OFFSET(iprop3d_texture_view, repeats, 12); OFFSET(iprop3d_texture_view, key, 16);
OFFSET(iprop3d_texture_view, name, 24); OFFSET(iprop3d_texture_view, asset_width, 40);
OFFSET(iprop3d_texture_view, asset_height, 44); OFFSET(iprop3d_texture_view, width, 48);
OFFSET(iprop3d_texture_view, height, 52); OFFSET(iprop3d_texture_view, byte_count, 56);
OFFSET(iprop3d_texture_view, row_stride, 64); OFFSET(iprop3d_texture_view, pixel_format, 72);
OFFSET(iprop3d_texture_view, alpha_mode, 76); OFFSET(iprop3d_texture_view, origin, 80);
OFFSET(iprop3d_texture_view, image_count, 84); OFFSET(iprop3d_texture_view, pixels, 88);
#endif

static iprop_utf8_view text(const char* value) {
    iprop_utf8_view view;
    view.data = value;
    view.byte_count = (uint32_t)strlen(value);
    return view;
}

static int same_name(iprop_utf8_view name, const char* expected) {
    return name.byte_count == strlen(expected) && memcmp(name.data, expected, name.byte_count) == 0;
}

static int load_model(const char* directory, const char* name, iprop3d_model** model) {
    char path[1024];
    uint8_t* bytes; size_t length;
    CHECK(snprintf(path, sizeof(path), "%s/%s", directory, name) < (int)sizeof(path));
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop3d_model_load(bytes, (uint64_t)length, model, NULL) == IPROP_EX_OK);
    /* The model owns its copy: the caller's bytes may go. */
    memset(bytes, 0xee, length);
    free(bytes);
    return 0;
}

static int info_of(const iprop3d_result* result, iprop3d_result_info* info) {
    CHECK(iprop3d_result_info_init(info, sizeof(*info)) == IPROP_EX_OK);
    CHECK(iprop3d_result_query(result, info, NULL) == IPROP_EX_OK);
    return 0;
}

static int surface_of(const iprop3d_result* result, uint32_t index, iprop3d_surface_view* view) {
    CHECK(iprop3d_surface_view_init(view, sizeof(*view)) == IPROP_EX_OK);
    CHECK(iprop3d_result_surface(result, index, view, NULL) == IPROP_EX_OK);
    return 0;
}

static int texture_of(const iprop3d_result* result, uint32_t index, iprop3d_texture_view* view) {
    CHECK(iprop3d_texture_view_init(view, sizeof(*view)) == IPROP_EX_OK);
    CHECK(iprop3d_result_texture(result, index, view, NULL) == IPROP_EX_OK);
    return 0;
}

/* Every index names a vertex, normals and tangents are unit vectors and the
 * tangent sign is one of the two it can be. */
static int well_formed(const iprop3d_surface_view* surface) {
    uint32_t i;
    CHECK(surface->index_count % 3 == 0);
    for (i = 0; i < surface->index_count; ++i) CHECK(surface->indices[i] < surface->vertex_count);
    for (i = 0; i < surface->vertex_count; ++i) {
        const float* n = surface->normals + (size_t)i * 3;
        const float* t = surface->tangents + (size_t)i * 4;
        const float nn = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
        const float tt = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];
        CHECK(nn > 0.999f && nn < 1.001f && tt > 0.999f && tt < 1.001f);
        CHECK(t[3] == 1.0f || t[3] == -1.0f);
    }
    return 0;
}

int main(int argc, char** argv) {
    static const iprop_color_rgba8 stain = {0x33, 0x66, 0x99, 0xff};
    char path[1024];
    uint8_t* bytes; size_t length;
    iprop3d_library_info library;
    iprop3d_model *crate = NULL, *empty = NULL;
    iprop3d_model_info model_info;
    iprop3d_skin_program_info skin;
    iprop_input_info_ex input;
    iprop_parameters *parameters = NULL, *tile_parameters = NULL;
    iprop3d_generate_options options;
    iprop3d_result *first = NULL, *second = NULL, *narrow = NULL, *bare = NULL, *nothing = NULL;
    iprop3d_result_info info, other_info;
    iprop3d_surface_view surface;
    iprop3d_texture_view texture, other_texture;
    iprop_aux_image_view_ex image;
    iprop_program* tile = NULL;
    iprop_generate_options_ex tile_options;
    iprop_result* tile_result = NULL;
    iprop_image_view_ex tile_image;
    iprop_aux_image_view_ex tile_normal;
    uint64_t fixed_key, seeded_key, crate_fuel;
    uint32_t count = 99, index = 99;
    CHECK(argc == 2);
    CHECK(IPROP3D_API_VERSION_MAJOR == 0 && IPROP3D_API_VERSION_MINOR == 1);
    CHECK(sizeof(iprop3d_alpha_mode) == 4 && IPROP3D_ALPHA_OPAQUE == 0 && IPROP3D_ALPHA_CUTOUT == 1 && IPROP3D_ALPHA_BLEND == 2);

    /* The library. */
    CHECK(iprop3d_library_info_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(library.struct_size == sizeof(library) && library.api_minor == 0 && library.max_texels == 0);
    CHECK(iprop3d_library_query(&library, NULL) == IPROP_EX_OK);
    CHECK(library.api_major == 0 && library.api_minor == 1);
    CHECK(library.max_vertices == 65536 && library.max_triangles == 131072);
    CHECK(library.max_textures == 32 && library.max_texture_side == 2048 && library.max_texels == 4194304);

    /* What a model declares. */
    CHECK(load_model(argv[1], "crate.iprop3d", &crate) == 0);
    CHECK(load_model(argv[1], "empty.iprop3d", &empty) == 0);
    CHECK(iprop3d_model_info_init(&model_info, sizeof(model_info)) == IPROP_EX_OK);
    CHECK(iprop3d_model_query(crate, &model_info, NULL) == IPROP_EX_OK);
    CHECK(model_info.size_x == 12.0f && model_info.size_y == 10.0f && model_info.size_z == 8.0f);
    CHECK(model_info.units_per_meter == 50.0f && model_info.fuel > 0);
    CHECK(model_info.input_count == 4 && model_info.skin_program_count == 1);
    crate_fuel = model_info.fuel;
    CHECK(iprop3d_model_query(empty, &model_info, NULL) == IPROP_EX_OK);
    CHECK(model_info.size_x == 1.0f && model_info.size_y == 2.0f && model_info.size_z == 3.0f);
    CHECK(model_info.units_per_meter == 100.0f && model_info.input_count == 0 && model_info.skin_program_count == 0);

    CHECK(iprop3d_skin_program_info_init(&skin, sizeof(skin)) == IPROP_EX_OK);
    CHECK(iprop3d_model_skin_program(crate, 0, &skin, NULL) == IPROP_EX_OK);
    CHECK(skin.program_index == 0 && skin.width == 16 && skin.height == 8 && same_name(skin.name, "tile"));
    CHECK(iprop3d_model_skin_program(crate, 1, &skin, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_model_skin_program(empty, 0, &skin, NULL) == IPROP_EX_E_ARGUMENT);

    /* Inputs are the 2D descriptors. */
    CHECK(iprop3d_model_input_count(crate, &count, NULL) == IPROP_EX_OK && count == 4);
    CHECK(iprop3d_model_input_count(empty, &count, NULL) == IPROP_EX_OK && count == 0);
    CHECK(iprop_input_info_ex_init(&input, sizeof(input)) == IPROP_EX_OK);
    CHECK(iprop3d_model_input_get(crate, 0, &input, NULL) == IPROP_EX_OK);
    CHECK(input.input_index == 0 && input.type == IPROP_INPUT_F32 && same_name(input.name, "wide"));
    CHECK(input.f32_default == 8.0f);
    CHECK(iprop3d_model_input_get(crate, 1, &input, NULL) == IPROP_EX_OK);
    CHECK(input.type == IPROP_INPUT_COLOR && same_name(input.name, "stain"));
    CHECK(memcmp(&input.color_default, &stain, 4) == 0);
    CHECK(iprop3d_model_input_get(crate, 2, &input, NULL) == IPROP_EX_OK);
    CHECK(input.type == IPROP_INPUT_U32 && same_name(input.name, "extra") && input.u32_default == 1);
    CHECK(iprop3d_model_input_get(crate, 4, &input, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_model_input_find(crate, text("stain"), &index, NULL) == IPROP_EX_OK && index == 1);
    CHECK(iprop3d_model_input_find(crate, text("rounding"), &index, NULL) == IPROP_EX_OK && index == 3);
    CHECK(iprop3d_model_input_find(crate, text("tint"), &index, NULL) == IPROP_EX_E_ARGUMENT && index == 3);

    /* One generation with the defaults. */
    CHECK(iprop3d_generate_options_init(&options, sizeof(options)) == IPROP_EX_OK);
    CHECK(options.struct_size == sizeof(options) && options.flags == 0 && options.seed == 0);
    options.seed = 5;
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, &first, NULL) == IPROP_EX_OK && first != NULL);
    CHECK(info_of(first, &info) == 0);
    CHECK(info.vertex_count == 24 + 96 + 4 + 3 && info.triangle_count == 12 + 44 + 2 + 1);
    CHECK(info.surface_count == 4 && info.texture_count == 2);
    CHECK(info.bounds_min[0] == -4.0f && info.bounds_min[1] == 0.0f && info.bounds_min[2] == -4.0f);
    CHECK(info.bounds_max[0] == 4.0f && info.bounds_max[1] == 10.0f && info.bounds_max[2] == 4.0f);
    CHECK(info.size[0] == 12.0f && info.size[1] == 10.0f && info.size[2] == 8.0f && info.units_per_meter == 50.0f);
    CHECK(info.fuel_budget == crate_fuel && info.fuel_used > 0 && info.fuel_used <= info.fuel_budget);
    CHECK(info.work_budget == IPROP_DEFAULT_WORK_LIMIT && info.work_used > 32 * 16 && info.work_used < info.work_budget);

    /* The box: the texture repeats at one texel to a unit. */
    CHECK(surface_of(first, 0, &surface) == 0 && well_formed(&surface) == 0);
    CHECK(surface.surface_index == 0 && surface.texture_index == 0);
    CHECK(surface.vertex_count == 24 && surface.index_count == 36);
    CHECK(surface.roughness == 0.8f && surface.metallic == 0.0f && surface.emission == 0.0f);
    CHECK(surface.alpha_mode == IPROP3D_ALPHA_OPAQUE && surface.alpha_cutoff == 0.5f);
    CHECK(surface.two_sided == 0 && surface.crisp == 0);
    /* Its front face starts at the lower left: x0 y0 z1, and three indices in. */
    CHECK(surface.positions[0] == -4.0f && surface.positions[1] == 0.0f && surface.positions[2] == 4.0f);
    CHECK(surface.normals[0] == 0.0f && surface.normals[1] == 0.0f && surface.normals[2] == 1.0f);
    CHECK(surface.tangents[0] == 1.0f && surface.tangents[3] == 1.0f);
    CHECK(surface.indices[0] == 0 && surface.indices[1] == 1 && surface.indices[2] == 2);
    /* Eight units of width on a sixteen-unit texture: u runs to one half. */
    CHECK(surface.uvs[0] == 0.0f && surface.uvs[2] == 0.5f);

    CHECK(surface_of(first, 1, &surface) == 0 && well_formed(&surface) == 0);
    CHECK(surface.texture_index == 1 && surface.roughness == 0.25f);
    CHECK(surface.vertex_count == 96 && surface.index_count == 44 * 3);

    /* The quad wears the whole image once, cut out and two-sided. */
    CHECK(surface_of(first, 2, &surface) == 0 && well_formed(&surface) == 0);
    CHECK(surface.texture_index == 0 && surface.vertex_count == 4 && surface.index_count == 6);
    CHECK(surface.alpha_mode == IPROP3D_ALPHA_CUTOUT && surface.alpha_cutoff == 0.5f && surface.two_sided == 1);
    CHECK(surface.uvs[0] == 0.0f && surface.uvs[1] == 1.0f && surface.uvs[4] == 1.0f && surface.uvs[5] == 0.0f);

    CHECK(surface_of(first, 3, &surface) == 0 && well_formed(&surface) == 0);
    CHECK(surface.vertex_count == 3 && surface.index_count == 3 && surface.crisp == 1);
    CHECK(surface.metallic == 0.75f && surface.emission == 1.5f && surface.two_sided == 0);
    CHECK(iprop3d_result_surface(first, 4, &surface, NULL) == IPROP_EX_E_ARGUMENT);

    /* Textures: the fixed one at its requested size, the seeded one as declared. */
    CHECK(texture_of(first, 0, &texture) == 0);
    CHECK(texture.texture_index == 0 && same_name(texture.name, "tile") && texture.repeats == 1);
    CHECK(texture.asset_width == 16 && texture.asset_height == 8 && texture.width == 32 && texture.height == 16);
    CHECK(texture.byte_count == 32 * 16 * 4 && texture.row_stride == 32 * 4 && texture.pixels != NULL);
    CHECK(texture.pixel_format == IPROP_PIXEL_FORMAT_RGBA8 && texture.origin == IPROP_IMAGE_ORIGIN_TOP_LEFT);
    CHECK(texture.alpha_mode == IPROP_IMAGE_ALPHA_LEGACY_DESTINATION && texture.image_count == 1);
    fixed_key = texture.key;
    CHECK(texture_of(first, 1, &other_texture) == 0);
    CHECK(other_texture.width == 16 && other_texture.height == 8 && other_texture.byte_count == 16 * 8 * 4);
    seeded_key = other_texture.key;
    CHECK(fixed_key != seeded_key);
    CHECK(iprop3d_result_texture(first, 2, &other_texture, NULL) == IPROP_EX_E_ARGUMENT);

    CHECK(iprop3d_result_texture_image_find(first, 0, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_OK && index == 0);
    CHECK(iprop3d_result_texture_image_find(first, 0, 99, &index, NULL) == IPROP_EX_E_ARGUMENT && index == 0);
    CHECK(iprop3d_result_texture_image_find(first, 2, IPROP_AUX_IMAGE_NORMAL, &index, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop_aux_image_view_ex_init(&image, sizeof(image)) == IPROP_EX_OK);
    CHECK(iprop3d_result_texture_image(first, 0, 0, &image, NULL) == IPROP_EX_OK);
    CHECK(image.image_index == 0 && image.kind == IPROP_AUX_IMAGE_NORMAL && image.source == IPROP_AUX_IMAGE_AUTHORED);
    CHECK(image.width == 32 && image.height == 16 && image.byte_count == 32 * 16 * 4 && image.row_stride == 128);
    CHECK(image.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE && same_name(image.name, "relief") && image.pixels != NULL);
    CHECK(iprop3d_result_texture_image(first, 0, 1, &image, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_result_texture_image(first, 2, 0, &image, NULL) == IPROP_EX_E_ARGUMENT);

    /* The fixed texture is what the 2D API makes of the skin's own program
     * for that seed, size and inputs: pixels and normal image, byte for byte. */
    CHECK(snprintf(path, sizeof(path), "%s/tile.iprop", argv[1]) < (int)sizeof(path));
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, &tile, NULL) == IPROP_EX_OK);
    free(bytes);
    CHECK(iprop_parameters_create(tile, &tile_parameters, NULL) == IPROP_EX_OK);
    CHECK(iprop_parameters_set_color_named(tile_parameters, text("tint"), stain, NULL) == IPROP_EX_OK);
    CHECK(iprop_generate_options_ex_init(&tile_options, sizeof(tile_options)) == IPROP_EX_OK);
    tile_options.seed = 77; tile_options.size_x = 32; tile_options.size_y = 16;
    CHECK(iprop_generate_ex(tile, &tile_options, tile_parameters, &tile_result, NULL) == IPROP_EX_OK);
    CHECK(iprop_image_view_ex_init(&tile_image, sizeof(tile_image)) == IPROP_EX_OK);
    CHECK(iprop_result_query_image(tile_result, &tile_image, NULL) == IPROP_EX_OK);
    CHECK(tile_image.byte_count == texture.byte_count);
    CHECK(memcmp(tile_image.pixels, texture.pixels, (size_t)texture.byte_count) == 0);
    CHECK(iprop_aux_image_view_ex_init(&tile_normal, sizeof(tile_normal)) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_get(tile_result, 0, &tile_normal, NULL) == IPROP_EX_OK);
    CHECK(tile_normal.byte_count == image.byte_count);
    CHECK(memcmp(tile_normal.pixels, image.pixels, (size_t)image.byte_count) == 0);
    /* A parameter set of another program, here the skin's, is foreign. */
    CHECK(iprop3d_generate(crate, &options, tile_parameters, NULL, &nothing, NULL) == IPROP_EX_E_ARGUMENT && nothing == NULL);

    /* Another seed: the fixed texture keeps its key and its pixels, the
     * seeded one changes; the shape changes with the seed too. */
    options.seed = 6;
    CHECK(iprop3d_generate(crate, &options, NULL, NULL, &second, NULL) == IPROP_EX_OK);
    CHECK(texture_of(second, 0, &other_texture) == 0);
    CHECK(other_texture.key == fixed_key && other_texture.pixels != texture.pixels);
    CHECK(memcmp(other_texture.pixels, texture.pixels, (size_t)texture.byte_count) == 0);
    CHECK(texture_of(second, 1, &other_texture) == 0 && other_texture.key != seeded_key);
    CHECK(surface_of(second, 0, &surface) == 0);
    {
        iprop3d_surface_view original;
        CHECK(surface_of(first, 0, &original) == 0);
        CHECK(memcmp(original.positions, surface.positions, 24 * 3 * sizeof(float)) != 0);
    }

    /* Parameters: an ordinary set, changed by name and by ID. */
    CHECK(iprop3d_model_parameters_create(crate, &parameters, NULL) == IPROP_EX_OK && parameters != NULL);
    CHECK(iprop_parameters_set_f32_named(parameters, text("wide"), 6.0f, NULL) == IPROP_EX_OK);
    options.seed = 5;
    CHECK(iprop3d_generate(crate, &options, parameters, NULL, &narrow, NULL) == IPROP_EX_OK);
    CHECK(info_of(narrow, &other_info) == 0);
    /* The box is narrower; the triangle still reaches x = 4. */
    CHECK(other_info.bounds_min[0] == -3.0f && other_info.bounds_max[0] == 4.0f);
    CHECK(other_info.vertex_count == info.vertex_count && other_info.surface_count == 4);
    CHECK(iprop_parameters_set_u32(parameters, 2, 0, NULL) == IPROP_EX_OK);
    CHECK(iprop3d_generate(crate, &options, parameters, NULL, &bare, NULL) == IPROP_EX_OK);
    CHECK(info_of(bare, &other_info) == 0);
    CHECK(other_info.vertex_count == 120 && other_info.triangle_count == 56 && other_info.surface_count == 2);
    CHECK(other_info.bounds_max[0] == 3.0f && other_info.bounds_max[1] == 7.0f && other_info.texture_count == 2);
    /* A changed colour reaches the texture: other pixels under another key. */
    {
        static const iprop_color_rgba8 red = {0xc0, 0x20, 0x20, 0xff};
        iprop3d_result* tinted = NULL;
        CHECK(iprop_parameters_set_color(parameters, 1, red, NULL) == IPROP_EX_OK);
        CHECK(iprop3d_generate(crate, &options, parameters, NULL, &tinted, NULL) == IPROP_EX_OK);
        CHECK(texture_of(tinted, 0, &other_texture) == 0);
        CHECK(other_texture.key != fixed_key);
        CHECK(memcmp(other_texture.pixels, texture.pixels, (size_t)texture.byte_count) != 0);
        iprop3d_result_free(tinted);
    }

    /* A model without triangles is a valid, empty result. */
    CHECK(iprop3d_generate(empty, &options, NULL, NULL, &nothing, NULL) == IPROP_EX_OK);
    CHECK(info_of(nothing, &other_info) == 0);
    CHECK(other_info.vertex_count == 0 && other_info.triangle_count == 0);
    CHECK(other_info.surface_count == 0 && other_info.texture_count == 0);
    CHECK(other_info.bounds_min[0] == 0.0f && other_info.bounds_max[1] == 0.0f && other_info.size[2] == 3.0f);
    CHECK(iprop3d_result_surface(nothing, 0, &surface, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(iprop3d_result_texture(nothing, 0, &other_texture, NULL) == IPROP_EX_E_ARGUMENT);

    /* Results and parameter sets outlive the model. */
    iprop3d_model_free(crate);
    iprop3d_model_free(empty);
    CHECK(texture_of(first, 0, &other_texture) == 0 && other_texture.key == fixed_key);
    CHECK(same_name(other_texture.name, "tile"));
    CHECK(memcmp(tile_image.pixels, other_texture.pixels, (size_t)texture.byte_count) == 0);
    CHECK(iprop_parameters_set_f32(parameters, 0, 9.0f, NULL) == IPROP_EX_OK);

    iprop3d_result_free(first); iprop3d_result_free(second); iprop3d_result_free(narrow);
    iprop3d_result_free(bare); iprop3d_result_free(nothing); iprop3d_result_free(NULL);
    iprop_parameters_free(parameters); iprop_parameters_free(tile_parameters);
    iprop_result_free(tile_result); iprop_program_free(tile);
    iprop3d_model_free(NULL);
    puts("capi3d models: OK");
    return 0;
}
