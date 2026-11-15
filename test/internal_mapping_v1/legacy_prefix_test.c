/* Compile/run with each supported sdkconfig profile, including segment mode. */
#include <stddef.h>
#include "private/elf_types.h"
typedef struct legacy_elf_prefix {
#ifdef CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    unsigned char *ptext;
    unsigned char *pdata;
#else
    unsigned char *psegment;
    uint32_t svaddr;
#endif
    esp_elf_sec_t sec[ELF_SECS];
    int (*entry)(int, char *[]);
#ifdef CONFIG_ELF_LOADER_SET_MMU
    uint32_t text_off, mmu_off, mmu_num;
#endif
#ifdef CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
    uint16_t num;
    esp_symtab_t *symtab;
#endif
} legacy_elf_prefix;
#define SAME(member) _Static_assert(offsetof(esp_elf_t, member) == offsetof(legacy_elf_prefix, member), #member)
#ifdef CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
SAME(ptext); SAME(pdata);
#else
SAME(psegment); SAME(svaddr);
#endif
SAME(sec); SAME(entry);
#ifdef CONFIG_ELF_LOADER_SET_MMU
SAME(text_off); SAME(mmu_off); SAME(mmu_num);
#endif
#ifdef CONFIG_ELF_DYNAMIC_LOAD_SHARED_OBJECT
SAME(num); SAME(symtab);
#endif
_Static_assert(ELF_SECS == 5, "legacy section slots");
_Static_assert(offsetof(esp_elf_t, internal_v1) >= sizeof(legacy_elf_prefix), "append-only suffix");
int main(void) { return 0; }
