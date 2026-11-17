#include "iprop.h"
#include "check.h"

/* API 0.8 material maps (spec material maps v1.2): the kinds, the capability,
 * the declarations in kind order and every published image of the shared
 * binding fixture, whose source declares them out of order. */

static int same_name(iprop_utf8_view name, const char* expected) {
    return name.byte_count == strlen(expected) && memcmp(name.data, expected, name.byte_count) == 0;
}

static int load(const char* path, iprop_program** program) {
    uint8_t* bytes; size_t length;
    CHECK(read_fixture(path, &bytes, &length) == 0);
    CHECK(iprop_program_load_ex(bytes, (uint64_t)length, program, NULL) == IPROP_EX_OK);
    free(bytes);
    return 0;
}

int main(int argc, char** argv) {
    static const iprop_aux_image_kind kinds[] = {IPROP_AUX_IMAGE_NORMAL, IPROP_AUX_IMAGE_ROUGHNESS,
        IPROP_AUX_IMAGE_METALLIC, IPROP_AUX_IMAGE_AO, IPROP_AUX_IMAGE_EMISSION};
    static const char* names[] = {"relief", "gloss", "metal", "shade", "glow"};
    iprop_library_info_ex library;
    iprop_program *maps = NULL, *plain = NULL;
    iprop_result *result = NULL, *derived = NULL, *none = NULL;
    iprop_generate_options_ex options;
    iprop_derived_normal_ex request;
    iprop_aux_image_info_ex info;
    iprop_aux_image_view_ex view, again;
    uint32_t count = 99, index = 99, i, p;
    CHECK(argc == 3);
    CHECK(IPROP_AUX_IMAGE_ROUGHNESS == 2 && IPROP_AUX_IMAGE_METALLIC == 3 && IPROP_AUX_IMAGE_AO == 4 &&
          IPROP_AUX_IMAGE_EMISSION == 5 && IPROP_CAP_MATERIAL_MAPS == (UINT64_C(1) << 23));
    CHECK(iprop_library_info_ex_init(&library, sizeof(library)) == IPROP_EX_OK);
    CHECK(iprop_library_query(&library) == IPROP_EX_OK && library.api_major == 0 && library.api_minor >= 8);
    CHECK((library.capabilities & IPROP_CAP_MATERIAL_MAPS) != 0 && (library.capabilities & IPROP_CAP_AUX_IMAGES) != 0);
    CHECK(load(argv[1], &maps) == 0 && load(argv[2], &plain) == 0);

    /* Declarations: one of each kind, in kind order. */
    CHECK(iprop_program_aux_image_count(maps, &count, NULL) == IPROP_EX_OK && count == 5);
    for (i = 0; i < 5; ++i) {
        CHECK(iprop_aux_image_info_ex_init(&info, sizeof(info)) == IPROP_EX_OK);
        CHECK(iprop_program_aux_image_get(maps, i, &info, NULL) == IPROP_EX_OK);
        CHECK(info.image_index == i && info.kind == kinds[i] && same_name(info.name, names[i]));
    }
    CHECK(iprop_program_aux_image_get(maps, 5, &info, NULL) == IPROP_EX_E_ARGUMENT);
    CHECK(info.image_index == 4 && info.kind == IPROP_AUX_IMAGE_EMISSION);

    /* Every image: tags, layout, opaque alpha and grey scalar maps. */
    CHECK(iprop_generate_options_ex_init(&options, sizeof(options)) == IPROP_EX_OK);
    options.seed = 5;
    CHECK(iprop_generate_ex(maps, &options, NULL, &result, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(result, &count, NULL) == IPROP_EX_OK && count == 5);
    for (i = 0; i < 5; ++i) {
        CHECK(iprop_aux_image_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK);
        CHECK(iprop_result_aux_image_get(result, i, &view, NULL) == IPROP_EX_OK);
        CHECK(view.image_index == i && view.kind == kinds[i] && view.source == IPROP_AUX_IMAGE_AUTHORED &&
              same_name(view.name, names[i]));
        CHECK(view.width == 8 && view.height == 4 && view.row_stride == 32 && view.byte_count == 128 &&
              view.pixel_format == IPROP_PIXEL_FORMAT_RGBA8 && view.alpha_mode == IPROP_IMAGE_ALPHA_OPAQUE &&
              view.origin == IPROP_IMAGE_ORIGIN_TOP_LEFT);
        for (p = 0; p < 128; p += 4) {
            CHECK(view.pixels[p + 3] == 255);
            if (kinds[i] == IPROP_AUX_IMAGE_ROUGHNESS || kinds[i] == IPROP_AUX_IMAGE_METALLIC || kinds[i] == IPROP_AUX_IMAGE_AO)
                CHECK(view.pixels[p] == view.pixels[p + 1] && view.pixels[p] == view.pixels[p + 2]);
        }
        CHECK(iprop_result_aux_image_find(result, kinds[i], &index, NULL) == IPROP_EX_OK && index == i);
        /* Hand-computed bytes: the painted value, the background and a blend. */
        if (kinds[i] == IPROP_AUX_IMAGE_ROUGHNESS)
            CHECK(view.pixels[0] == 64 && view.pixels[4 * 4] == 255 && view.pixels[(3 * 8 + 5) * 4] == 192);
        if (kinds[i] == IPROP_AUX_IMAGE_METALLIC)
            CHECK(view.pixels[2 * 4] == 255 && view.pixels[0] == 0 && view.pixels[(3 * 8 + 7) * 4] == 128);
        if (kinds[i] == IPROP_AUX_IMAGE_AO)
            CHECK(view.pixels[0] == 255 && view.pixels[2 * 8 * 4] == 191);
        if (kinds[i] == IPROP_AUX_IMAGE_EMISSION)
            CHECK(view.pixels[(8 + 6) * 4] == 255 && view.pixels[(8 + 6) * 4 + 1] == 160 && view.pixels[(8 + 6) * 4 + 2] == 32 &&
                  view.pixels[(8 + 5) * 4] == 64 && view.pixels[0] == 0);
    }
    CHECK(iprop_result_aux_image_get(result, 5, &view, NULL) == IPROP_EX_E_ARGUMENT);

    /* An authored normal map wins over a derive request; the maps stay. */
    CHECK(iprop_derived_normal_ex_init(&request, sizeof(request)) == IPROP_EX_OK);
    CHECK(iprop_generate_derived_normal(maps, &options, NULL, NULL, &request, &derived, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_count(derived, &count, NULL) == IPROP_EX_OK && count == 5);
    for (i = 0; i < 5; ++i) {
        CHECK(iprop_aux_image_view_ex_init(&view, sizeof(view)) == IPROP_EX_OK && iprop_aux_image_view_ex_init(&again, sizeof(again)) == IPROP_EX_OK);
        CHECK(iprop_result_aux_image_get(result, i, &view, NULL) == IPROP_EX_OK);
        CHECK(iprop_result_aux_image_get(derived, i, &again, NULL) == IPROP_EX_OK);
        CHECK(again.kind == view.kind && again.source == IPROP_AUX_IMAGE_AUTHORED && memcmp(again.pixels, view.pixels, 128) == 0);
    }

    /* A program without maps publishes none, and find says so. */
    CHECK(iprop_generate_ex(plain, &options, NULL, &none, NULL) == IPROP_EX_OK);
    CHECK(iprop_result_aux_image_find(none, IPROP_AUX_IMAGE_ROUGHNESS, &index, NULL) == IPROP_EX_E_ARGUMENT);

    iprop_result_free(result); iprop_result_free(derived); iprop_result_free(none);
    iprop_program_free(maps); iprop_program_free(plain);
    puts("C99 material maps: kinds, capability, declarations in kind order, every image byte rule, find and the derive request: PASS");
    return 0;
}
