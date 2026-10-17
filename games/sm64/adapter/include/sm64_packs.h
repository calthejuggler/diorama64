/* One pack per kind is enabled: DynOS lets the higher index win for a shared actor. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
enum { SM64_PACK_NONE = 0, SM64_PACK_TEXTURES = 1, SM64_PACK_MODELS = 2 };
typedef struct { const char *name; int kind; } sm64_pack_state;
int sm64_packs_kind(int tex_files, int bin_files);
const char *sm64_packs_kind_name(int kind);
int sm64_packs_pick(const sm64_pack_state *p, int n, int kind, const char *want);
void sm64_packs_plan(const sm64_pack_state *p, int n, int kind, int chosen, int *want_on);
/* 0 original, 1 on (name: "" = the default pick, or the pack name), -1 bad (also a name with '/', "..", empty, or not
 * fitting cap). */
int sm64_packs_parse_choice(int kind, const char *s, char *name, int cap);
int sm64_packs_choice_string(int kind, int on, const char *name, char *buf, int cap);
int sm64_packs_trees_3d(int trees, int trees_3d);
#ifdef __cplusplus
}
#endif
