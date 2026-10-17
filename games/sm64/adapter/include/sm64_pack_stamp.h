/* On the device a pack loads only when its stamp parses and every listed file is a regular file with that sha256;
 * unlisted files never load. This stops an unprepared pack, not a deliberate forger. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define SM64_PACK_STAMP_FILE ".sm64ar-verified"
#define SM64_PACK_STAMP_MAX_FILES 8192
#define SM64_PACK_STAMP_NAME_MAX 128
typedef struct {
    char (*names)[SM64_PACK_STAMP_NAME_MAX];
    int n, tex, bins;
    double mb;
} sm64_pack_stamp;
/* 1: dir's stamp is valid and every listed file matches (out filled; free with sm64_pack_stamp_free). 0: refused, why in
 * err (out left empty). A listed name must end in .tex or .bin, hold no '/', not start with '.', and appear once. */
int sm64_pack_stamp_load(const char *dir, sm64_pack_stamp *out, char *err, int errcap);
int sm64_pack_stamp_lists(const sm64_pack_stamp *s, const char *file);
void sm64_pack_stamp_free(sm64_pack_stamp *s);
#ifdef __cplusplus
}
#endif
