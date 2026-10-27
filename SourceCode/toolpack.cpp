typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned short WORD;
typedef void* HANDLE;
typedef void* HINSTANCE;
typedef unsigned long uintptr_t;
typedef unsigned long uint32_t;
typedef __int64 int64_t;

#define TRUE 1
#define FALSE 0
#define NULL 0

extern "C" {
    void* __cdecl memset(void* dest, int c, unsigned int count);
    void* __cdecl memcpy(void* dest, const void* src, unsigned int count);
}

#pragma function(memset, memcpy)

extern "C" {
    void* __cdecl memset(void* dest, int c, unsigned int count) {
        char* bytes = (char*)dest;
        while (count--) {
            *bytes++ = (char)c;
        }
        return dest;
    }
    void* __cdecl memcpy(void* dest, const void* src, unsigned int count) {
        char* d = (char*)dest;
        const char* s = (const char*)src;
        while (count--) {
            *d++ = *s++;
        }
        return dest;
    }
}

static unsigned int StrLen(const char* s) {
    if (!s) return 0;
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static char ToUpper(char c) {
    return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

static void IntToStr(int val, char* out) {
    if (val == 0) { out[0] = '0'; out[1] = 0; return; }
    char buf[16];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = 0; i < idx; ++i) {
        out[i] = buf[idx - 1 - i];
    }
    out[idx] = 0;
}

template<class T> static T ReadMem(uintptr_t p) { return *reinterpret_cast<T*>(p); }

struct tgcx_RenameString {
    char data[16];
    uint32_t length;
    uint32_t capacity;
    uint32_t reserved;
};

static tgcx_RenameString tgcx_RenameBorrow(const char* value) {
    tgcx_RenameString s;
    memset(&s, 0, sizeof(s));
    s.length = StrLen(value);
    s.capacity = 15;
    if (s.length < 16) {
        for (unsigned i = 0; i < s.length; ++i) s.data[i] = value[i];
        s.data[s.length] = 0;
    } else {
        *reinterpret_cast<const char**>(s.data) = value;
        s.capacity = s.length;
    }
    return s;
}

struct StdVectorString {
    tgcx_RenameString* begin;
    tgcx_RenameString* end;
    tgcx_RenameString* cap;
};

struct V2ModAPI {
    uint32_t size, apiVersion, gameBase;
    const char* modFolder;
    void(__cdecl* Log)(const char*, ...);
    int(__cdecl* PatchSlot)(uint32_t, int, void*, void**);
    int(__cdecl* StealToTrampoline)(uint32_t, unsigned, unsigned char*, int, const unsigned char*, void*, void**, const char*);
};

static uintptr_t g_base = 0;
static const V2ModAPI* g_api = nullptr;
static uintptr_t g_update_orig = 0;
static void* g_destroy_orig = nullptr;

#pragma section(".shared", read, write, execute)
__declspec(allocate(".shared")) static unsigned char g_tramp[64];

enum PickTarget {
    PICK_NONE = 0,
    PICK_TRANSFER = 1,
    PICK_SPHERE = 2,
    PICK_VASSAL = 3
};

struct ToolpackState {
    uintptr_t view;
    uintptr_t root;
    uintptr_t entry_btn;
    uintptr_t window;
    int last_prov_id;
    bool is_open;
    int pick_target;
    bool entry_placed;
    bool window_placed;

    char sphere_tag[8];
    int sphere_id;

    char vassal_tag[8];
    int vassal_id;

    char transfer_tag[8];
    int transfer_id;

    bool bound[20];
    unsigned char glue[20][44];
};

static ToolpackState g_state;

static void CallConsole1(uintptr_t rva, const char* a1) {
    tgcx_RenameString arg1 = tgcx_RenameBorrow(a1);
    StdVectorString vec;
    vec.begin = &arg1;
    vec.end = &arg1 + 1;
    vec.cap = &arg1 + 1;
    tgcx_RenameString result;
    memset(&result, 0, sizeof(result));
    typedef void(__cdecl* CmdFn)(tgcx_RenameString*, const StdVectorString*);
    reinterpret_cast<CmdFn>(g_base + rva)(&result, &vec);
}

static void CallConsole2(uintptr_t rva, const char* a1, const char* a2) {
    tgcx_RenameString args[2];
    args[0] = tgcx_RenameBorrow(a1);
    args[1] = tgcx_RenameBorrow(a2);
    StdVectorString vec;
    vec.begin = &args[0];
    vec.end = &args[2];
    vec.cap = &args[2];
    tgcx_RenameString result;
    memset(&result, 0, sizeof(result));
    typedef void(__cdecl* CmdFn)(tgcx_RenameString*, const StdVectorString*);
    reinterpret_cast<CmdFn>(g_base + rva)(&result, &vec);
}

static uintptr_t FindGuiChild(uintptr_t host, unsigned slot, uint32_t expected, const char* key) {
    if (!host) return 0;
    auto* table = ReadMem<uintptr_t*>(host);
    if (!table || table[slot] != g_base + expected) return 0;
    auto name = tgcx_RenameBorrow(key);
    typedef uintptr_t(__fastcall* FindFn)(uintptr_t, void*, const void*);
    return reinterpret_cast<FindFn>(table[slot])(host, nullptr, &name);
}

static bool ReadEditBoxText(uintptr_t edit, char* out, unsigned capacity) {
    if (!edit) return false;
    uintptr_t buffer = edit + 0x54;
    const auto* s = reinterpret_cast<const tgcx_RenameString*>(buffer + 0x18);
    if (!s || s->length >= capacity || s->capacity < s->length || s->capacity > 1048576) return false;
    const char* text = s->capacity > 15 ? *reinterpret_cast<const char* const*>(s->data) : s->data;
    if (!text) return false;
    for (unsigned i = 0; i < s->length; ++i) out[i] = text[i];
    out[s->length] = 0;
    return true;
}

static void SetEditBoxText(uintptr_t edit, const char* str) {
    if (!edit) return;
    uintptr_t buffer = edit + 0x54;
    auto* vt = ReadMem<uintptr_t*>(buffer);
    if (!vt || vt[16] != g_base + 0x5F5290) return;
    auto text = tgcx_RenameBorrow(str);
    typedef uintptr_t(__fastcall* SetTextFn)(uintptr_t, void*, const void*);
    reinterpret_cast<SetTextFn>(vt[16])(buffer, nullptr, &text);
    reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_base + 0x5B6400)(edit);
}

static bool IsValidWindow(uintptr_t win) {
    if (!win) return false;
    return ReadMem<uintptr_t>(win) == g_base + 0xA3C0D8;
}

static void SetGuiVisible(uintptr_t window, bool show) {
    if (!IsValidWindow(window)) return;
    auto* vt = ReadMem<uintptr_t*>(window);
    if (show) {
        if (ReadMem<unsigned char>(window + 0x67)) return;
        uintptr_t gui = window + 0x18;
        auto* secondary = ReadMem<uintptr_t*>(gui);
        if (secondary && secondary[13] == g_base + 0x5E4490) {
            reinterpret_cast<void(__fastcall*)(uintptr_t, void*)>(secondary[13])(gui, nullptr);
            return;
        }
    } else {
        if (!ReadMem<unsigned char>(window + 0x67)) return;
        if (vt[9] == g_base + 0x5E4030) {
            reinterpret_cast<void(__fastcall*)(uintptr_t, void*)>(vt[9])(window, nullptr);
            return;
        }
    }
}

static bool PlaceWindow(uintptr_t window, int x, int y, bool promote) {
    if (!IsValidWindow(window) || x < 0 || y < 0) return false;
    auto* vt = ReadMem<uintptr_t*>(window);
    if (vt[6] != g_base + 0x5E3BA0 || vt[11] != g_base + 0x5E2FC0) return false;
    if (promote) reinterpret_cast<void(__fastcall*)(uintptr_t, void*)>(vt[11])(window, nullptr);
    typedef void(__fastcall* PositionFn)(uintptr_t, void*, uint32_t);
    reinterpret_cast<PositionFn>(vt[6])(window, nullptr, static_cast<unsigned>(x) | (static_cast<unsigned>(y) << 16));
    return true;
}

static uintptr_t LookupDefinition(uintptr_t registry, const char* key) {
    auto name = tgcx_RenameBorrow(key);
    auto* reg = ReadMem<uintptr_t*>(registry);
    if (!reg || reg[1] < g_base) return 0;
    typedef uintptr_t(__fastcall* LookupFn)(uintptr_t, void*, const void*);
    return reinterpret_cast<LookupFn>(reg[1])(registry, nullptr, &name);
}

static uintptr_t MakeGuiWindow(uintptr_t context, const char* key) {
    if (!context || !LookupDefinition(context + 0x34, key)) return 0;
    auto name = tgcx_RenameBorrow(key);
    auto* vt = ReadMem<uintptr_t*>(context);
    if (vt[11] != g_base + 0x5A38F0) return 0;
    typedef uintptr_t(__fastcall* MakeFn)(uintptr_t, void*, const void*, uintptr_t);
    uintptr_t win = reinterpret_cast<MakeFn>(vt[11])(context, nullptr, &name, 0);
    if (win && ReadMem<uintptr_t>(win) != g_base + 0xA3C0D8) return 0;
    SetGuiVisible(win, false);
    return win;
}

struct SphereCountryPair {
    uint32_t tag;
    int id;
};

static int TagToCountryId(const char* tag) {
    if (!tag || StrLen(tag) < 3) return 0;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    if (!manager) return 0;

    SphereCountryPair pair;
    pair.tag = 0;
    pair.id = 0;
    typedef void(__stdcall* GetCountryFn)(uintptr_t, SphereCountryPair*, const char*);
    reinterpret_cast<GetCountryFn>(g_base + 0x15C6C0)(manager, &pair, tag);
    if (pair.id > 0) return pair.id;

    int num_countries = ReadMem<int>(manager);
    uintptr_t countries = ReadMem<uintptr_t>(manager + 4);
    if (!countries) return 0;

    char t0 = ToUpper(tag[0]);
    char t1 = ToUpper(tag[1]);
    char t2 = ToUpper(tag[2]);

    for (int i = 1; i < num_countries; ++i) {
        uintptr_t c = ReadMem<uintptr_t>(countries + i * 4);
        if (c) {
            const char* ctag = reinterpret_cast<const char*>(c + 0x1C);
            if (ToUpper(ctag[0]) == t0 && ToUpper(ctag[1]) == t1 && ToUpper(ctag[2]) == t2) {
                return i;
            }
        }
    }
    return 0;
}

static void MakeVassal(int master_id, int target_id) {
    if (master_id <= 0 || target_id <= 0 || master_id == target_id) return;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    uintptr_t countries = manager ? ReadMem<uintptr_t>(manager + 4) : 0;
    if (!countries) return;
    uintptr_t master = ReadMem<uintptr_t>(countries + master_id * 4);
    uintptr_t target = ReadMem<uintptr_t>(countries + target_id * 4);
    if (!master || !target) return;

    uintptr_t rel_m = ReadMem<uintptr_t>(master + 0xBE8);
    uintptr_t rel_t = ReadMem<uintptr_t>(target + 0xBE8);
    if (!rel_m || !rel_t) return;
    uintptr_t rel_to_target = ReadMem<uintptr_t>(rel_m + target_id * 4);
    uintptr_t rel_to_master = ReadMem<uintptr_t>(rel_t + master_id * 4);
    if (!rel_to_target || !rel_to_master) return;

    // Release from previous master if any
    int num_countries = ReadMem<int>(manager);
    for (int i = 1; i < num_countries; ++i) {
        if (i == master_id || i == target_id) continue;
        uintptr_t other = ReadMem<uintptr_t>(countries + i * 4);
        if (!other) continue;
        uintptr_t rel_o = ReadMem<uintptr_t>(other + 0xBE8);
        if (!rel_o) continue;
        uintptr_t rel_o_t = ReadMem<uintptr_t>(rel_o + target_id * 4);
        if (rel_o_t) {
            uintptr_t old_act = ReadMem<uintptr_t>(rel_o_t + 0x24);
            if (old_act) {
                uintptr_t on_revoke = g_base + 0x52CFF0;
                __asm {
                    push ebx
                    push esi
                    push edi
                    mov ecx, old_act
                    call on_revoke
                    pop edi
                    pop esi
                    pop ebx
                }
            }
            *reinterpret_cast<BYTE*>(rel_o_t + 0x70) = 0;
            *reinterpret_cast<uintptr_t*>(rel_o_t + 0x24) = 0;
        }
    }

    // Allocate CVassalAction (size 0x24) using game operator new
    uintptr_t new_fn = g_base + 0x6AE9AF;
    uintptr_t action = reinterpret_cast<uintptr_t(__cdecl*)(size_t)>(new_fn)(0x24);
    if (!action) return;

    // Call base constructor
    uintptr_t ctor = g_base + 0x17FB40;
    __asm {
        push ebx
        push esi
        push edi
        mov eax, action
        call ctor
        pop edi
        pop esi
        pop ebx
    }

    // Set CVassalAction vtable
    *reinterpret_cast<uintptr_t*>(action) = g_base + 0xA01790;

    // Initialize fields
    *reinterpret_cast<uint32_t*>(action + 0x08) = ReadMem<uint32_t>(master + 0x1C);
    *reinterpret_cast<int*>(action + 0x0C) = master_id;
    *reinterpret_cast<uint32_t*>(action + 0x10) = ReadMem<uint32_t>(target + 0x1C);
    *reinterpret_cast<int*>(action + 0x14) = target_id;
    uintptr_t world = ReadMem<uintptr_t>(g_base + 0xE588E8);
    *reinterpret_cast<int*>(action + 0x18) = world ? ReadMem<int>(world + 0xB0C) : 0;

    // Call CVassalAction::OnEstablish (vtable[10]) to link into relations properly
    uintptr_t on_establish = g_base + 0x52CF60;
    __asm {
        push ebx
        push esi
        push edi
        mov ecx, action
        call on_establish
        pop edi
        pop esi
        pop ebx
    }

    // Crucial: In Victoria 2, rel_to_target + 0x70 is 0 for master (master is not a vassal!),
    // and rel_to_master + 0x70 is 2 for target (target is satellite of master!).
    *reinterpret_cast<BYTE*>(rel_to_target + 0x70) = 0; // Master is NOT a vassal
    *reinterpret_cast<BYTE*>(rel_to_master + 0x70) = 2; // Target IS satellite
    *reinterpret_cast<BYTE*>(rel_to_target + 0x71) = 1;
    *reinterpret_cast<BYTE*>(rel_to_master + 0x71) = 1;

    // Insert into CWorld active agreements linked list (world + 0xA84)
    if (world) {
        uintptr_t list = world + 0xA84;
        uintptr_t node = reinterpret_cast<uintptr_t(__cdecl*)(size_t)>(new_fn)(0x10);
        if (node) {
            uintptr_t tail = ReadMem<uintptr_t>(list + 4);
            *reinterpret_cast<uintptr_t*>(node + 0) = action;
            *reinterpret_cast<uintptr_t*>(node + 4) = tail;
            *reinterpret_cast<uintptr_t*>(node + 8) = 0;
            *reinterpret_cast<BYTE*>(node + 12) = 0;

            *reinterpret_cast<uintptr_t*>(list + 4) = node;
            *reinterpret_cast<int*>(list + 8) += 1;
            if (tail) {
                *reinterpret_cast<uintptr_t*>(tail + 8) = node;
            } else {
                *reinterpret_cast<uintptr_t*>(list + 0) = node;
            }
        }
    }

    // Refresh UI & notifications
    SphereCountryPair m_pair = { ReadMem<uint32_t>(master + 0x1C), master_id };
    SphereCountryPair t_pair = { ReadMem<uint32_t>(target + 0x1C), target_id };
    uintptr_t refresh_fn = g_base + 0x3DCF30;
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&m_pair);
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&t_pair);
}

static void RemoveVassal(int master_id, int target_id) {
    if (target_id <= 0) return;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    uintptr_t countries = manager ? ReadMem<uintptr_t>(manager + 4) : 0;
    if (!countries) return;
    uintptr_t target = ReadMem<uintptr_t>(countries + target_id * 4);
    if (!target) return;

    uintptr_t master = (master_id > 0 && master_id != target_id) ? ReadMem<uintptr_t>(countries + master_id * 4) : 0;
    uintptr_t rel_m = master ? ReadMem<uintptr_t>(master + 0xBE8) : 0;
    uintptr_t rel_t = ReadMem<uintptr_t>(target + 0xBE8);
    if (!rel_t) return;

    uintptr_t rel_to_target = (rel_m && master_id > 0) ? ReadMem<uintptr_t>(rel_m + target_id * 4) : 0;
    uintptr_t rel_to_master = (rel_t && master_id > 0) ? ReadMem<uintptr_t>(rel_t + master_id * 4) : 0;

    uintptr_t action = rel_to_target ? ReadMem<uintptr_t>(rel_to_target + 0x24) : 0;
    if (!action && rel_to_master) action = ReadMem<uintptr_t>(rel_to_master + 0x24);

    if (!action) {
        int num_countries = ReadMem<int>(manager);
        for (int i = 1; i < num_countries; ++i) {
            if (i == target_id) continue;
            uintptr_t other = ReadMem<uintptr_t>(countries + i * 4);
            if (!other) continue;
            uintptr_t rel_o = ReadMem<uintptr_t>(other + 0xBE8);
            if (!rel_o) continue;
            uintptr_t rel_o_t = ReadMem<uintptr_t>(rel_o + target_id * 4);
            if (rel_o_t && ReadMem<uintptr_t>(rel_o_t + 0x24)) {
                master_id = i;
                master = other;
                rel_m = rel_o;
                rel_to_target = rel_o_t;
                rel_to_master = ReadMem<uintptr_t>(rel_t + master_id * 4);
                action = ReadMem<uintptr_t>(rel_o_t + 0x24);
                break;
            }
        }
    }
    if (!master || master_id <= 0) return;

    if (action) {
        uintptr_t on_revoke = g_base + 0x52CFF0;
        __asm {
            push ebx
            push esi
            push edi
            mov ecx, action
            call on_revoke
            pop edi
            pop esi
            pop ebx
        }

        uintptr_t world = ReadMem<uintptr_t>(g_base + 0xE588E8);
        if (world) {
            uintptr_t list = world + 0xA84;
            uintptr_t cur = ReadMem<uintptr_t>(list + 0);
            while (cur) {
                if (ReadMem<uintptr_t>(cur + 0) == action) {
                    uintptr_t prev = ReadMem<uintptr_t>(cur + 4);
                    uintptr_t next = ReadMem<uintptr_t>(cur + 8);
                    if (prev) *reinterpret_cast<uintptr_t*>(prev + 8) = next;
                    else *reinterpret_cast<uintptr_t*>(list + 0) = next;
                    if (next) *reinterpret_cast<uintptr_t*>(next + 4) = prev;
                    else *reinterpret_cast<uintptr_t*>(list + 4) = prev;
                    *reinterpret_cast<int*>(list + 8) -= 1;
                    break;
                }
                cur = ReadMem<uintptr_t>(cur + 8);
            }
        }
    }

    *reinterpret_cast<BYTE*>(rel_to_target + 0x70) = 0;
    *reinterpret_cast<BYTE*>(rel_to_master + 0x70) = 0;
    *reinterpret_cast<uintptr_t*>(rel_to_target + 0x24) = 0;
    *reinterpret_cast<uintptr_t*>(rel_to_master + 0x24) = 0;
    *reinterpret_cast<BYTE*>(rel_to_target + 0x71) = 1;
    *reinterpret_cast<BYTE*>(rel_to_master + 0x71) = 1;

    SphereCountryPair m_pair = { ReadMem<uint32_t>(master + 0x1C), master_id };
    SphereCountryPair t_pair = { ReadMem<uint32_t>(target + 0x1C), target_id };
    uintptr_t refresh_fn = g_base + 0x3DCF30;
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&m_pair);
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&t_pair);
}

static void AddToSphere(int master_id, int target_id) {
    if (master_id <= 0 || target_id <= 0 || master_id == target_id) return;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    uintptr_t countries = manager ? ReadMem<uintptr_t>(manager + 4) : 0;
    if (!countries) return;
    uintptr_t master = ReadMem<uintptr_t>(countries + master_id * 4);
    uintptr_t target = ReadMem<uintptr_t>(countries + target_id * 4);
    if (!master || !target) return;

    uint32_t master_tag = ReadMem<uint32_t>(master + 0x1C);
    uint32_t target_tag = ReadMem<uint32_t>(target + 0x1C);

    // 1. Remove from previous master's sphere if any
    int old_master_id = ReadMem<int>(target + 0x142C);
    if (old_master_id > 0 && old_master_id != master_id) {
        uintptr_t old_master = ReadMem<uintptr_t>(countries + old_master_id * 4);
        if (old_master) {
            uintptr_t b = ReadMem<uintptr_t>(old_master + 0x1418);
            uintptr_t e = ReadMem<uintptr_t>(old_master + 0x141C);
            for (uintptr_t cur = b; cur < e; cur += 8) {
                if (ReadMem<int>(cur + 4) == target_id) {
                    for (uintptr_t p = cur; p + 8 < e; p += 8) {
                        *reinterpret_cast<uint32_t*>(p) = ReadMem<uint32_t>(p + 8);
                        *reinterpret_cast<int*>(p + 4) = ReadMem<int>(p + 12);
                    }
                    *reinterpret_cast<uintptr_t*>(old_master + 0x141C) = e - 8;
                    break;
                }
            }
            uintptr_t old_rel = ReadMem<uintptr_t>(old_master + 0xBE8);
            if (old_rel) {
                uintptr_t rel_o_t = ReadMem<uintptr_t>(old_rel + target_id * 4);
                if (rel_o_t) *reinterpret_cast<int*>(rel_o_t + 0x40) = 0;
            }
        }
    }

    // 2. Set target's sphere leader
    *reinterpret_cast<uint32_t*>(target + 0x1428) = master_tag;
    *reinterpret_cast<int*>(target + 0x142C) = master_id;

    // 3. Add to master's sphered countries vector (master + 0x1418) if not present
    SphereCountryPair pair;
    pair.tag = target_tag;
    pair.id = target_id;

    bool already_in = false;
    uintptr_t b = ReadMem<uintptr_t>(master + 0x1418);
    uintptr_t e = ReadMem<uintptr_t>(master + 0x141C);
    for (uintptr_t cur = b; cur < e; cur += 8) {
        if (ReadMem<int>(cur + 4) == target_id) {
            already_in = true;
            break;
        }
    }
    if (!already_in) {
        uintptr_t push_fn = g_base + 0xD61D0; // std::vector::push_back
        uintptr_t vec = master + 0x1418;
        void* pPair = &pair;
        __asm {
            push ebx
            push esi
            push edi
            mov edi, vec
            mov eax, pPair
            call push_fn
            pop edi
            pop esi
            pop ebx
        }
    }

    // 4. Update diplomatic relations status to 5 (In Sphere) on both sides
    uintptr_t world = ReadMem<uintptr_t>(g_base + 0xE588E8);
    int cur_date = world ? ReadMem<int>(world + 0xB0C) : 0;

    uintptr_t rel_m = ReadMem<uintptr_t>(master + 0xBE8);
    uintptr_t rel_t = ReadMem<uintptr_t>(target + 0xBE8);
    if (rel_m) {
        uintptr_t rel_to_target = ReadMem<uintptr_t>(rel_m + target_id * 4);
        if (rel_to_target) {
            *reinterpret_cast<int*>(rel_to_target + 0x40) = 5; // In Sphere
            *reinterpret_cast<int*>(rel_to_target + 0x44) = cur_date;
        }
    }
    if (rel_t) {
        uintptr_t rel_to_master = ReadMem<uintptr_t>(rel_t + master_id * 4);
        if (rel_to_master) {
            *reinterpret_cast<int*>(rel_to_master + 0x40) = 5; // In Sphere
            *reinterpret_cast<int*>(rel_to_master + 0x44) = cur_date;
        }
    }

    // 5. Call native engine AddToSphere (0x133e50)
    uintptr_t fn = g_base + 0x133E50;
    void* pPair = &pair;
    __asm {
        push ebx
        push esi
        push edi
        mov esi, pPair
        push master
        call fn
        pop edi
        pop esi
        pop ebx
    }

    // 6. Refresh UI & diplomacy views
    SphereCountryPair m_pair = { master_tag, master_id };
    SphereCountryPair t_pair = { target_tag, target_id };
    uintptr_t refresh_fn = g_base + 0x3DCF30;
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&m_pair);
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&t_pair);
}

static void RemoveFromSphere(int master_id, int target_id) {
    if (target_id <= 0) return;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    uintptr_t countries = manager ? ReadMem<uintptr_t>(manager + 4) : 0;
    if (!countries) return;
    uintptr_t target = ReadMem<uintptr_t>(countries + target_id * 4);
    if (!target) return;

    int cur_leader = ReadMem<int>(target + 0x142C);
    if (cur_leader > 0) {
        master_id = cur_leader;
    }
    if (master_id <= 0 || master_id == target_id) return;
    uintptr_t master = ReadMem<uintptr_t>(countries + master_id * 4);
    if (!master) return;

    uint32_t master_tag = ReadMem<uint32_t>(master + 0x1C);
    uint32_t target_tag = ReadMem<uint32_t>(target + 0x1C);

    if (ReadMem<int>(target + 0x142C) == master_id) {
        *reinterpret_cast<uint32_t*>(target + 0x1428) = 0;
        *reinterpret_cast<int*>(target + 0x142C) = 0;
    }

    uintptr_t b = ReadMem<uintptr_t>(master + 0x1418);
    uintptr_t e = ReadMem<uintptr_t>(master + 0x141C);
    for (uintptr_t cur = b; cur < e; cur += 8) {
        if (ReadMem<int>(cur + 4) == target_id) {
            for (uintptr_t p = cur; p + 8 < e; p += 8) {
                *reinterpret_cast<uint32_t*>(p) = ReadMem<uint32_t>(p + 8);
                *reinterpret_cast<int*>(p + 4) = ReadMem<int>(p + 12);
            }
            *reinterpret_cast<uintptr_t*>(master + 0x141C) = e - 8;
            break;
        }
    }

    uintptr_t rel_m = ReadMem<uintptr_t>(master + 0xBE8);
    uintptr_t rel_t = ReadMem<uintptr_t>(target + 0xBE8);
    if (rel_m) {
        uintptr_t rel_to_target = ReadMem<uintptr_t>(rel_m + target_id * 4);
        if (rel_to_target && ReadMem<int>(rel_to_target + 0x40) == 5) {
            *reinterpret_cast<int*>(rel_to_target + 0x40) = 0;
        }
    }
    if (rel_t) {
        uintptr_t rel_to_master = ReadMem<uintptr_t>(rel_t + master_id * 4);
        if (rel_to_master && ReadMem<int>(rel_to_master + 0x40) == 5) {
            *reinterpret_cast<int*>(rel_to_master + 0x40) = 0;
        }
    }

    SphereCountryPair pair;
    pair.tag = target_tag;
    pair.id = target_id;
    uintptr_t fn = g_base + 0x133F20;
    void* pPair = &pair;
    __asm {
        push ebx
        push esi
        push edi
        mov edi, pPair
        push master
        call fn
        pop edi
        pop esi
        pop ebx
    }

    SphereCountryPair m_pair = { master_tag, master_id };
    SphereCountryPair t_pair = { target_tag, target_id };
    uintptr_t refresh_fn = g_base + 0x3DCF30;
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&m_pair);
    reinterpret_cast<void(__cdecl*)(const void*)>(refresh_fn)(&t_pair);
}

static void GrabProvinceOwnerTagToEdit(uintptr_t prov, const char* edit_name, int target_type) {
    if (!prov) return;
    int owner_id = ReadMem<int>(prov + 0x12C);
    if (owner_id <= 0) return;
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    uintptr_t countries = manager ? ReadMem<uintptr_t>(manager + 4) : 0;
    uintptr_t country = countries ? ReadMem<uintptr_t>(countries + owner_id * 4) : 0;
    if (!country) return;

    char tag_str[4];
    const char* ptag = reinterpret_cast<const char*>(country + 0x1C);
    for (int i = 0; i < 3; ++i) tag_str[i] = ptag[i];
    tag_str[3] = 0;

    if (target_type == PICK_TRANSFER) {
        for (int i = 0; i < 4; ++i) g_state.transfer_tag[i] = tag_str[i];
        g_state.transfer_id = owner_id;
        if (IsValidWindow(g_state.window)) {
            uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_tag");
            if (edit) SetEditBoxText(edit, tag_str);
        }
    } else if (target_type == PICK_SPHERE) {
        for (int i = 0; i < 4; ++i) g_state.sphere_tag[i] = tag_str[i];
        g_state.sphere_id = owner_id;
        if (IsValidWindow(g_state.window)) {
            uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_sphere_tag");
            if (edit) SetEditBoxText(edit, tag_str);
        }
    } else if (target_type == PICK_VASSAL) {
        for (int i = 0; i < 4; ++i) g_state.vassal_tag[i] = tag_str[i];
        g_state.vassal_id = owner_id;
        if (IsValidWindow(g_state.window)) {
            uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_vassal_tag");
            if (edit) SetEditBoxText(edit, tag_str);
        }
    }
}

static void DoAction(unsigned act) {
    if (act == 0) { // Toggle window
        g_state.is_open = !g_state.is_open;
        if (IsValidWindow(g_state.window)) {
            if (g_state.is_open) {
                if (!g_state.window_placed) {
                    PlaceWindow(g_state.window, 385, 260, true);
                    g_state.window_placed = true;
                }
                SetGuiVisible(g_state.window, true);
            } else {
                SetGuiVisible(g_state.window, false);
            }
        }
        return;
    }
    if (act == 1) { // Close window
        g_state.is_open = false;
        g_state.pick_target = PICK_NONE;
        if (IsValidWindow(g_state.window)) SetGuiVisible(g_state.window, false);
        return;
    }

    uintptr_t world = ReadMem<uintptr_t>(g_base + 0xE588E8);
    uintptr_t manager = ReadMem<uintptr_t>(g_base + 0xE587E4);
    if (!world || !manager) return;
    int player_id = ReadMem<int>(world + 0xB60);
    uintptr_t countries = ReadMem<uintptr_t>(manager + 4);
    uintptr_t player_country = countries ? ReadMem<uintptr_t>(countries + player_id * 4) : 0;
    if (!player_country) return;

    uintptr_t prov = 0;
    int prov_id = 0;
    if (g_state.view && ReadMem<uintptr_t>(g_state.view) == g_base + 0xA13768) {
        prov = ReadMem<uintptr_t>(g_state.view + 0x24);
        if (prov) prov_id = ReadMem<int>(prov + 0x58);
    }
    if (prov_id <= 0) prov_id = g_state.last_prov_id;

    if (act == 10) { // Pick Tag for Province Transfer
        g_state.pick_target = PICK_TRANSFER;
        if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != player_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_tag", PICK_TRANSFER);
                g_state.pick_target = PICK_NONE;
            }
        }
        return;
    }
    if (act == 14) { // Pick Tag for Sphere
        g_state.pick_target = PICK_SPHERE;
        if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != player_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_sphere_tag", PICK_SPHERE);
                g_state.pick_target = PICK_NONE;
            }
        }
        return;
    }
    if (act == 15) { // Pick Tag for Vassal
        g_state.pick_target = PICK_VASSAL;
        if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != player_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_vassal_tag", PICK_VASSAL);
                g_state.pick_target = PICK_NONE;
            }
        }
        return;
    }

    char prov_str[16];
    IntToStr(prov_id, prov_str);

    int master_id = player_id;
    char master_tag[16];
    memset(master_tag, 0, sizeof(master_tag));
    uintptr_t edit_master = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_tag");
    if (ReadEditBoxText(edit_master, master_tag, sizeof(master_tag)) && StrLen(master_tag) >= 3) {
        int m_id = TagToCountryId(master_tag);
        if (m_id > 0) master_id = m_id;
    } else if (StrLen(g_state.transfer_tag) >= 3) {
        int m_id = TagToCountryId(g_state.transfer_tag);
        if (m_id > 0) master_id = m_id;
    }

    if (act == 2) { // Annex / Transfer province
        if (prov_id <= 0) return;
        uintptr_t target_country = countries ? ReadMem<uintptr_t>(countries + master_id * 4) : 0;
        if (!target_country) target_country = player_country;

        char final_tag[4];
        const char* ptag = reinterpret_cast<const char*>(target_country + 0x1C);
        for (int i = 0; i < 3; ++i) final_tag[i] = ptag[i];
        final_tag[3] = 0;

        CallConsole2(0x20680, final_tag, prov_str); // changeowner <tag> <prov_id>
        CallConsole2(0x20940, final_tag, prov_str); // changecontroller <tag> <prov_id>
        if (g_state.view && g_update_orig) {
            reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_update_orig)(g_state.view);
        }
    }
    else if (act == 3) { // 3x Infantry
        if (prov_id <= 0) return;
        CallConsole2(0x1BB40, "infantry", prov_str);
        CallConsole2(0x1BB40, "infantry", prov_str);
        CallConsole2(0x1BB40, "infantry", prov_str);
    }
    else if (act == 4) { // 3x Cavalry
        if (prov_id <= 0) return;
        CallConsole2(0x1BB40, "cavalry", prov_str);
        CallConsole2(0x1BB40, "cavalry", prov_str);
        CallConsole2(0x1BB40, "cavalry", prov_str);
    }
    else if (act == 5) { // 3x Artillery
        if (prov_id <= 0) return;
        CallConsole2(0x1BB40, "artillery", prov_str);
        CallConsole2(0x1BB40, "artillery", prov_str);
        CallConsole2(0x1BB40, "artillery", prov_str);
    }
    else if (act == 6) { // 3x Tanks
        if (prov_id <= 0) return;
        CallConsole2(0x1BB40, "tank", prov_str);
        CallConsole2(0x1BB40, "tank", prov_str);
        CallConsole2(0x1BB40, "tank", prov_str);
    }
    else if (act == 7) { // Money
        CallConsole1(0x1FC60, "100000");
        int64_t* money_ptr = reinterpret_cast<int64_t*>(player_country + 0x90);
        if (money_ptr) *money_ptr += 100000LL * 1000LL;
    }
    else if (act == 8) { // All Goods
        CallConsole1(0x1D780, "1000");
    }
    else if (act == 9) { // Leadership
        CallConsole1(0x1DC00, "500");
        uintptr_t* rp = reinterpret_cast<uintptr_t*>(player_country + 0x98);
        if (rp) *rp += 500 * 1000;
    }
    else if (act == 11) { // Add to Sphere
        char tag_to_use[16];
        memset(tag_to_use, 0, sizeof(tag_to_use));
        uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_sphere_tag");
        if (ReadEditBoxText(edit, tag_to_use, sizeof(tag_to_use)) && StrLen(tag_to_use) >= 3) {
            for (int i = 0; i < 3; ++i) g_state.sphere_tag[i] = tag_to_use[i];
            g_state.sphere_tag[3] = 0;
        } else if (StrLen(g_state.sphere_tag) >= 3) {
            for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.sphere_tag[i];
            if (edit) SetEditBoxText(edit, tag_to_use);
        } else if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != master_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_sphere_tag", PICK_SPHERE);
                for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.sphere_tag[i];
            }
        }
        int target_id = TagToCountryId(tag_to_use);
        if (target_id > 0 && target_id != master_id) {
            AddToSphere(master_id, target_id);
        }
    }
    else if (act == 16) { // Remove from Sphere
        char tag_to_use[16];
        memset(tag_to_use, 0, sizeof(tag_to_use));
        uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_sphere_tag");
        if (ReadEditBoxText(edit, tag_to_use, sizeof(tag_to_use)) && StrLen(tag_to_use) >= 3) {
            for (int i = 0; i < 3; ++i) g_state.sphere_tag[i] = tag_to_use[i];
            g_state.sphere_tag[3] = 0;
        } else if (StrLen(g_state.sphere_tag) >= 3) {
            for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.sphere_tag[i];
            if (edit) SetEditBoxText(edit, tag_to_use);
        } else if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != master_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_sphere_tag", PICK_SPHERE);
                for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.sphere_tag[i];
            }
        }
        int target_id = TagToCountryId(tag_to_use);
        if (target_id > 0) {
            RemoveFromSphere(master_id, target_id);
        }
    }
    else if (act == 12) { // Make Vassal / Puppet
        char tag_to_use[16];
        memset(tag_to_use, 0, sizeof(tag_to_use));
        uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_vassal_tag");
        if (ReadEditBoxText(edit, tag_to_use, sizeof(tag_to_use)) && StrLen(tag_to_use) >= 3) {
            for (int i = 0; i < 3; ++i) g_state.vassal_tag[i] = tag_to_use[i];
            g_state.vassal_tag[3] = 0;
        } else if (StrLen(g_state.vassal_tag) >= 3) {
            for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.vassal_tag[i];
            if (edit) SetEditBoxText(edit, tag_to_use);
        } else if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != master_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_vassal_tag", PICK_VASSAL);
                for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.vassal_tag[i];
            }
        }
        int target_id = TagToCountryId(tag_to_use);
        if (target_id > 0 && target_id != master_id) {
            MakeVassal(master_id, target_id);
        }
    }
    else if (act == 17) { // Remove Vassal
        char tag_to_use[16];
        memset(tag_to_use, 0, sizeof(tag_to_use));
        uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_vassal_tag");
        if (ReadEditBoxText(edit, tag_to_use, sizeof(tag_to_use)) && StrLen(tag_to_use) >= 3) {
            for (int i = 0; i < 3; ++i) g_state.vassal_tag[i] = tag_to_use[i];
            g_state.vassal_tag[3] = 0;
        } else if (StrLen(g_state.vassal_tag) >= 3) {
            for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.vassal_tag[i];
            if (edit) SetEditBoxText(edit, tag_to_use);
        } else if (prov) {
            int owner_id = ReadMem<int>(prov + 0x12C);
            if (owner_id > 0 && owner_id != master_id) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_vassal_tag", PICK_VASSAL);
                for (int i = 0; i < 4; ++i) tag_to_use[i] = g_state.vassal_tag[i];
            }
        }
        int target_id = TagToCountryId(tag_to_use);
        if (target_id > 0) {
            RemoveVassal(master_id, target_id);
        }
    }
    else if (act == 13) { // Occupy Province
        if (prov_id <= 0) return;
        uintptr_t occ_country = countries ? ReadMem<uintptr_t>(countries + master_id * 4) : 0;
        if (!occ_country) occ_country = player_country;

        char occupier_tag[4];
        const char* ptag = reinterpret_cast<const char*>(occ_country + 0x1C);
        for (int i = 0; i < 3; ++i) occupier_tag[i] = ptag[i];
        occupier_tag[3] = 0;

        CallConsole2(0x20940, occupier_tag, prov_str); // changecontroller <tag> <prov_id>
        if (g_state.view && g_update_orig) {
            reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_update_orig)(g_state.view);
        }
    }
    else if (act == 18) { // Transfer Region / State
        if (prov_id <= 0 || !prov) return;
        uintptr_t target_country = countries ? ReadMem<uintptr_t>(countries + master_id * 4) : 0;
        if (!target_country) target_country = player_country;

        char final_tag[4];
        const char* ptag = reinterpret_cast<const char*>(target_country + 0x1C);
        for (int i = 0; i < 3; ++i) final_tag[i] = ptag[i];
        final_tag[3] = 0;

        static int s_region_provs[256];
        int reg_count = 0;

        // In Victoria 2, CProvince::GetRegion (VA 0x4B8A80):
        // if (prov + 0x164 != 0) region = *reinterpret_cast<uintptr_t*>(prov + 0x15C);
        uintptr_t region = 0;
        if (ReadMem<int>(prov + 0x164) != 0) {
            uintptr_t p15c = ReadMem<uintptr_t>(prov + 0x15C);
            if (p15c) {
                uintptr_t cand1 = p15c;
                uintptr_t cand2 = ReadMem<uintptr_t>(p15c);
                if (cand2 && ReadMem<uintptr_t>(cand2) == g_base + 0x9FD5E0) {
                    region = cand2;
                } else if (ReadMem<uintptr_t>(cand1) == g_base + 0x9FD5E0) {
                    region = cand1;
                } else if (cand2) {
                    region = cand2;
                }
            }
        }

        // CRegion stores a Paradox linked list of all provinces in this geographic region at region + 8
        if (region) {
            uintptr_t cur_node = ReadMem<uintptr_t>(region + 8);
            while (cur_node && reg_count < 256) {
                int pid = ReadMem<int>(cur_node); // node->prov_id
                if (pid > 0 && pid < 10000) {
                    bool dup = false;
                    for (int k = 0; k < reg_count; ++k) {
                        if (s_region_provs[k] == pid) { dup = true; break; }
                    }
                    if (!dup) s_region_provs[reg_count++] = pid;
                }
                cur_node = ReadMem<uintptr_t>(cur_node + 8); // node->next
            }
        }

        // Fallback: Check CState at prov + 0x188 if CRegion didn't yield provinces
        if (reg_count == 0) {
            uintptr_t state = ReadMem<uintptr_t>(prov + 0x188);
            if (state) {
                uintptr_t b = ReadMem<uintptr_t>(state + 0x48);
                uintptr_t e = ReadMem<uintptr_t>(state + 0x4C);
                if (e > b && (e - b) <= 64 * 4) {
                    int n = static_cast<int>((e - b) / 4);
                    for (int i = 0; i < n && reg_count < 256; ++i) {
                        int pid = ReadMem<int>(b + i * 4);
                        if (pid > 0 && pid < 10000) {
                            bool dup = false;
                            for (int k = 0; k < reg_count; ++k) {
                                if (s_region_provs[k] == pid) { dup = true; break; }
                            }
                            if (!dup) s_region_provs[reg_count++] = pid;
                        }
                    }
                }
            }
        }

        // Fallback to single clicked province
        if (reg_count == 0 && prov_id > 0) {
            s_region_provs[reg_count++] = prov_id;
        }

        for (int i = 0; i < reg_count; ++i) {
            char p_str[16];
            IntToStr(s_region_provs[i], p_str);
            CallConsole2(0x20680, final_tag, p_str); // changeowner <tag> <prov_id>
            CallConsole2(0x20940, final_tag, p_str); // changecontroller <tag> <prov_id>
        }

        if (g_state.view && g_update_orig) {
            reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_update_orig)(g_state.view);
        }
    }
    else if (act == 19) { // Annex Country
        uintptr_t target_country = countries ? ReadMem<uintptr_t>(countries + master_id * 4) : 0;
        if (!target_country) target_country = player_country;

        char final_tag[4];
        const char* ptag = reinterpret_cast<const char*>(target_country + 0x1C);
        for (int i = 0; i < 3; ++i) final_tag[i] = ptag[i];
        final_tag[3] = 0;

        int victim_id = 0;
        if (prov) {
            int p_owner = ReadMem<int>(prov + 0x12C);
            if (p_owner > 0 && p_owner != master_id) {
                victim_id = p_owner;
            }
        }
        if (victim_id <= 0) {
            if (g_state.vassal_id > 0 && g_state.vassal_id != master_id) {
                victim_id = g_state.vassal_id;
            } else if (g_state.sphere_id > 0 && g_state.sphere_id != master_id) {
                victim_id = g_state.sphere_id;
            }
        }

        if (victim_id <= 0 || victim_id == master_id) return;
        uintptr_t victim_country = ReadMem<uintptr_t>(countries + victim_id * 4);
        if (!victim_country) return;

        static int s_provs_to_annex[4096];
        int annex_count = 0;

        // Victoria 2 stores all world provinces in CWorld array at world + 0xACC
        // prov_begin: world + 0xACC, prov_end: world + 0xAD0
        uintptr_t prov_arr = ReadMem<uintptr_t>(world + 0xACC);
        uintptr_t prov_end = ReadMem<uintptr_t>(world + 0xAD0);
        int num_provinces = (prov_end > prov_arr) ? static_cast<int>((prov_end - prov_arr) / 4) : 4000;
        if (num_provinces <= 0 || num_provinces > 4096) num_provinces = 4096;

        if (prov_arr) {
            for (int i = 1; i < num_provinces && annex_count < 4096; ++i) {
                uintptr_t p = ReadMem<uintptr_t>(prov_arr + i * 4);
                // prov + 0x12C is owner country ID for EVERY province (homeland, colonies, protectorates, islands)
                if (p && ReadMem<int>(p + 0x12C) == victim_id) {
                    s_provs_to_annex[annex_count++] = i;
                }
            }
        }

        for (int i = 0; i < annex_count; ++i) {
            char p_str[16];
            IntToStr(s_provs_to_annex[i], p_str);
            CallConsole2(0x20680, final_tag, p_str); // changeowner <tag> <prov_id>
            CallConsole2(0x20940, final_tag, p_str); // changecontroller <tag> <prov_id>
        }

        if (g_state.view && g_update_orig) {
            reinterpret_cast<void(__stdcall*)(uintptr_t)>(g_update_orig)(g_state.view);
        }
    }
}

#define THUNK(n) \
__declspec(naked) static void Thunk##n() { \
    __asm { pushfd } __asm { pushad } __asm { mov ebx, esp } \
    __asm { sub esp, 528 } __asm { and esp, -16 } __asm { fxsave [esp] } \
    __asm { cld } __asm { push n } __asm { call DoAction } __asm { add esp, 4 } \
    __asm { fxrstor [esp] } __asm { mov esp, ebx } __asm { popad } __asm { popfd } __asm { ret } \
}

THUNK(0) THUNK(1) THUNK(2) THUNK(3) THUNK(4)
THUNK(5) THUNK(6) THUNK(7) THUNK(8) THUNK(9)
THUNK(10) THUNK(11) THUNK(12) THUNK(13)
THUNK(14) THUNK(15) THUNK(16) THUNK(17)
THUNK(18) THUNK(19)

static void (*const g_thunks[])() = {
    Thunk0, Thunk1, Thunk2, Thunk3, Thunk4,
    Thunk5, Thunk6, Thunk7, Thunk8, Thunk9,
    Thunk10, Thunk11, Thunk12, Thunk13,
    Thunk14, Thunk15, Thunk16, Thunk17,
    Thunk18, Thunk19
};

static bool BindButton(uintptr_t host, const char* btn_name, unsigned action_id) {
    if (action_id >= 20 || g_state.bound[action_id]) return true;
    uintptr_t btn = FindGuiChild(host, 13, 0x5E4C80, btn_name);
    if (!btn) return false;
    uintptr_t observable = btn + 0x54;
    auto* vt = ReadMem<uintptr_t*>(observable);
    if (!vt || vt[1] < g_base) return false;
    auto* glue = g_state.glue[action_id];
    memset(glue, 0, 44);
    *reinterpret_cast<uintptr_t*>(glue) = g_base + 0xA0E48C;
    *reinterpret_cast<uintptr_t*>(glue + 4) = reinterpret_cast<uintptr_t>(&g_state);
    *reinterpret_cast<uintptr_t*>(glue + 8) = reinterpret_cast<uintptr_t>(g_thunks[action_id]);
    typedef uintptr_t(__fastcall* RegObserver)(uintptr_t, void*, const void*);
    reinterpret_cast<RegObserver>(vt[1])(observable, nullptr, glue);
    g_state.bound[action_id] = true;
    return true;
}

static void OnGuiUpdate(uintptr_t view) {
    if (!view || ReadMem<uintptr_t>(view) != g_base + 0xA13768) return;
    g_state.view = view;

    uintptr_t prov = ReadMem<uintptr_t>(view + 0x24);
    if (prov) {
        int pid = ReadMem<int>(prov + 0x58);
        if (pid != g_state.last_prov_id) {
            g_state.last_prov_id = pid;
            if (g_state.pick_target == PICK_TRANSFER) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_tag", PICK_TRANSFER);
                g_state.pick_target = PICK_NONE;
            } else if (g_state.pick_target == PICK_SPHERE) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_sphere_tag", PICK_SPHERE);
                g_state.pick_target = PICK_NONE;
            } else if (g_state.pick_target == PICK_VASSAL) {
                GrabProvinceOwnerTagToEdit(prov, "toolpack_vassal_tag", PICK_VASSAL);
                g_state.pick_target = PICK_NONE;
            }
        }
    }

    uintptr_t root = ReadMem<uintptr_t>(view + 0x20);
    if (!root || !ReadMem<unsigned char>(root + 0x67)) {
        if (IsValidWindow(g_state.entry_btn)) SetGuiVisible(g_state.entry_btn, false);
        if (IsValidWindow(g_state.window) && !g_state.is_open) SetGuiVisible(g_state.window, false);
        return;
    }

    if (root != g_state.root) {
        g_state.root = root;
        g_state.entry_btn = 0;
        g_state.window = 0;
        g_state.entry_placed = false;
        g_state.window_placed = false;
        for (int i = 0; i < 20; ++i) g_state.bound[i] = false;
    }

    uintptr_t context = ReadMem<uintptr_t>(root + 0x4C);
    if (!context || ReadMem<uintptr_t>(context) != g_base + 0xA08A44) return;

    if (!IsValidWindow(g_state.entry_btn)) {
        g_state.entry_btn = MakeGuiWindow(context, "toolpack_entry");
        g_state.bound[0] = false;
        if (g_state.entry_btn) {
            BindButton(g_state.entry_btn, "toolpack_open", 0);
        }
    }
    if (!IsValidWindow(g_state.window)) {
        g_state.window = MakeGuiWindow(context, "toolpack_window");
        for (int i = 1; i < 20; ++i) g_state.bound[i] = false;
        if (g_state.window) {
            BindButton(g_state.window, "toolpack_close", 1);
            BindButton(g_state.window, "toolpack_annex", 2);
            BindButton(g_state.window, "toolpack_spawn_inf", 3);
            BindButton(g_state.window, "toolpack_spawn_cav", 4);
            BindButton(g_state.window, "toolpack_spawn_art", 5);
            BindButton(g_state.window, "toolpack_spawn_tank", 6);
            BindButton(g_state.window, "toolpack_money", 7);
            BindButton(g_state.window, "toolpack_goods", 8);
            BindButton(g_state.window, "toolpack_leadership", 9);
            BindButton(g_state.window, "toolpack_pick_tag", 10);
            BindButton(g_state.window, "toolpack_add_sphere", 11);
            BindButton(g_state.window, "toolpack_make_vassal", 12);
            BindButton(g_state.window, "toolpack_occupy", 13);
            BindButton(g_state.window, "toolpack_pick_sphere", 14);
            BindButton(g_state.window, "toolpack_pick_vassal", 15);
            BindButton(g_state.window, "toolpack_rem_sphere", 16);
            BindButton(g_state.window, "toolpack_rem_vassal", 17);
            BindButton(g_state.window, "toolpack_transfer_region", 18);
            BindButton(g_state.window, "toolpack_annex_country", 19);

            if (StrLen(g_state.sphere_tag) >= 3) {
                uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_sphere_tag");
                if (edit) SetEditBoxText(edit, g_state.sphere_tag);
            }
            if (StrLen(g_state.vassal_tag) >= 3) {
                uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_vassal_tag");
                if (edit) SetEditBoxText(edit, g_state.vassal_tag);
            }
            if (StrLen(g_state.transfer_tag) >= 3) {
                uintptr_t edit = FindGuiChild(g_state.window, 16, 0x5E4D20, "toolpack_tag");
                if (edit) SetEditBoxText(edit, g_state.transfer_tag);
            }
        }
    }

    if (IsValidWindow(g_state.entry_btn) && !g_state.entry_placed) {
        PlaceWindow(g_state.entry_btn, 385, 220, false);
        SetGuiVisible(g_state.entry_btn, true);
        g_state.entry_placed = true;
    }
    if (IsValidWindow(g_state.window) && g_state.is_open && !g_state.window_placed) {
        PlaceWindow(g_state.window, 385, 260, false);
        SetGuiVisible(g_state.window, true);
        g_state.window_placed = true;
    }
}

static void OnProvinceDestroy(uintptr_t view) {
    if (g_state.view == view || !view) {
        g_state.view = 0;
        g_state.root = 0;
        g_state.entry_btn = 0;
        g_state.window = 0;
        g_state.entry_placed = false;
        g_state.window_placed = false;
        // Keep is_open, pick_target, and saved country tags intact!
        for (int i = 0; i < 20; ++i) g_state.bound[i] = false;
    }
}

__declspec(naked) static void UpdateHook() {
    __asm { pushfd } __asm { pushad } __asm { mov eax, [esp + 40] } __asm { mov ebx, esp }
    __asm { sub esp, 528 } __asm { and esp, -16 } __asm { fxsave [esp] }
    __asm { cld } __asm { push eax } __asm { call OnGuiUpdate } __asm { add esp, 4 }
    __asm { fxrstor [esp] } __asm { mov esp, ebx } __asm { popad } __asm { popfd }
    __asm { jmp g_update_orig }
}

__declspec(naked) static void DestroyHook() {
    __asm { pushfd } __asm { pushad } __asm { mov eax, ecx } __asm { mov ebx, esp }
    __asm { sub esp, 528 } __asm { and esp, -16 } __asm { fxsave [esp] }
    __asm { cld } __asm { push eax } __asm { call OnProvinceDestroy } __asm { add esp, 4 }
    __asm { fxrstor [esp] } __asm { mov esp, ebx } __asm { popad } __asm { popfd }
    __asm { jmp g_destroy_orig }
}

#pragma comment(linker, "/export:V2Mod_Init=_V2Mod_Init")
extern "C" __declspec(dllexport) int __cdecl V2Mod_Init(const V2ModAPI* api) {
    if (!api || api->apiVersion != 1) return 0;
    g_api = api;
    g_base = api->gameBase;

    const unsigned char expect[] = { 0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8 };
    void* orig = nullptr;
    if (!api->StealToTrampoline(0x340050, 6, g_tramp, 32, expect, (void*)UpdateHook, &orig, "Toolpack")) {
        return 0;
    }
    g_update_orig = (uintptr_t)orig;

    void* old_destr = nullptr;
    if (api->PatchSlot(0xA13768, 0, (void*)DestroyHook, &old_destr)) {
        g_destroy_orig = old_destr;
    }

    if (api->Log) api->Log("Victoria 2 Toolpack: Initialized successfully via Reverse Engineering!");
    return 1;
}

BOOL __stdcall DllMain(HINSTANCE h, DWORD r, void* res) {
    return TRUE;
}
