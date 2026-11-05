/*
 * TDU_DealerOwnedGreen.c  -  ASI plugin for Test Drive Unlimited (PC, TestDriveUnlimited.exe 1.66A,
 * sha256 1408778de813902587ad83438bce52a606f8895c5d74478762ad1c9cf92d5a96)
 *
 * At a car or bike dealer, shows the name of every vehicle the player already owns in GREEN.
 * (The game itself only uses red = not in stock at this dealer, blue = reserved.)
 *
 * ---------------------------------------------------------------------------------------------
 * How the dealer colours the vehicle name (static analysis of the shipped exe + shops.bnk)
 * ---------------------------------------------------------------------------------------------
 * The dealer screen is the Flash movie FrontEnd/<Hi|Low>Res/shops.bnk. Its "infos" clip has one
 * frame per state, and each frame contains its OWN "/:Current_car" EditText with a baked colour:
 *      frame "default"   char 0x49  /:Current_car  FFFFFF   (in stock, normal dealer)
 *      frame "red"       char 0x4D  /:Current_car  FF0000   + /:m_OutOfStock (char 0x54)
 *      frame "blue"      char 0x55  /:Current_car  00B5E1   + /:m_Reserved
 *      frame "available" char 0x59  /:Current_car  FFFFFF   (in stock, classic dealer)
 * The ActionScript picks the frame from "/:IsCarInStock", which the exe sets in
 *      DealerSelectCar  0x726110   (0 = out of stock -> red, 1 = reserved -> blue, 2 = in stock)
 * There is no green frame. However the engine's EditText renderer (0x44BE60 -> 0x99EC00) reads
 * the text colour straight from the EditText *definition* (def+0x20, RGBA) on every draw, so
 * changing that colour at run time recolours the name immediately.
 *
 * Engine structures used (all read-only except the colour bytes):
 *      [0x10E8080]                 head of the list of loaded Flash movie contexts
 *      ctx+0xAB0                   next context
 *      ctx+0x60 / ctx+0x64         character dictionary (char id -> definition*) / its size
 *      def+0x00 == 0xF3ECF8        vtable of DefineEditText (GetTag() = 0x25)
 *      def+0x08                    character id
 *      def+0x20                    text colour R,G,B,A
 *      def+0x32                    bound variable name ("/:Current_car")
 * Player garage (v1 wrongly used [0x11F2CDC], which is the catalogue of ALL vehicle configs):
 *      profile = [0x114C15C]->vtbl[0x24](); 640 slots of 0x80 bytes at profile+0x20;
 *      slot+0x24 = status (2 = empty), slot+0x18 = XTEA-encrypted vehicle id (key 0xF99F28,
 *      decryptor 0x6DDD70); the low dword equals the dealer's id dealer+0xFFA54[i].
 *
 * ---------------------------------------------------------------------------------------------
 * The hook
 * ---------------------------------------------------------------------------------------------
 * In DealerSelectCar, every selection of a vehicle ends with
 *      7264A0  mov ecx, [ebp+eax*4+0xFFA54]   ; selected vehicle id
 *      7264A9  mov [ebp+0x107C44], ecx        ; dealer->currentCar
 *      7264AF  call 0x7782A0
 * The call at 0x7264AF is redirected to a wrapper that (with all registers preserved) checks
 * whether vehicle id ECX is in one of the garage slots and then recolours the four "infos" /:Current_car fields:
 * green if owned, their original colour otherwise. The status line (out of stock / reserved)
 * and everything else on the screen is unchanged; buying behaviour is not touched.
 *
 * Install: copy TDU_DealerOwnedGreen.asi + TDU_DealerOwnedGreen.ini into the game's "scripts"
 *          folder (Ultimate ASI Loader = dinput8.dll).
 */

/* ------------------------------------------------------------------ minimal Win32 decls ---- */
typedef int            BOOL;
typedef unsigned long  DWORD;
typedef void*          HANDLE;
typedef void*          HMODULE;

#define WINAPI __stdcall
#define IMPORT __declspec(dllimport)

IMPORT BOOL    WINAPI VirtualProtect(void* addr, unsigned long size, DWORD prot, DWORD* old);
IMPORT HMODULE WINAPI GetModuleHandleA(const char* name);
IMPORT DWORD   WINAPI GetModuleFileNameA(HMODULE mod, char* buf, DWORD size);
IMPORT unsigned WINAPI GetPrivateProfileIntA(const char* app, const char* key, int def, const char* file);
IMPORT DWORD   WINAPI GetPrivateProfileStringA(const char* app, const char* key, const char* def,
                                               char* out, DWORD size, const char* file);
IMPORT HANDLE  WINAPI CreateFileA(const char* name, DWORD access, DWORD share, void* sa, DWORD disp, DWORD flags, HANDLE tmpl);
IMPORT BOOL    WINAPI WriteFile(HANDLE h, const void* buf, DWORD n, DWORD* written, void* ov);
IMPORT DWORD   WINAPI SetFilePointer(HANDLE h, long dist, long* high, DWORD method);
IMPORT BOOL    WINAPI CloseHandle(HANDLE h);
IMPORT BOOL    WINAPI FlushInstructionCache(HANDLE proc, const void* addr, unsigned long size);
IMPORT HANDLE  WINAPI GetCurrentProcess(void);
IMPORT BOOL    WINAPI DisableThreadLibraryCalls(HMODULE mod);

#define PAGE_EXECUTE_READWRITE 0x40
#define GENERIC_WRITE          0x40000000
#define FILE_SHARE_READ        0x00000001
#define OPEN_ALWAYS            4
#define CREATE_ALWAYS          2
#define FILE_END               2
#define INVALID_HANDLE_VALUE   ((HANDLE)(long)-1)

#ifdef __clang__
int _fltused = 0;   /* freestanding build: no CRT */
#endif

/* ------------------------------------------------------------------ game addresses -------- */
/* Image base 0x400000, no relocations: absolute VAs are fine.                                */
#define SITE_SELECT      0x007264AFu   /* call 0x7782A0 in DealerSelectCar (0x726110)        */
#define TARGET_SELECT    0x007782A0u
#define G_PROFILE_MGR    0x0114C15Cu   /* object whose vtbl[0x24] returns the player profile */
#define G_XTEA_KEY       0x00F99F28u   /* key used to obfuscate vehicle ids in garage slots  */
#define G_FLASH_CTX_HEAD 0x010E8080u   /* first loaded Flash movie context                   */
#define VT_EDITTEXT      0x00F3ECF8u   /* DefineEditText vtable                              */

/* Garage: 640 slot objects of 0x80 bytes at profile+0x20 (walked like this in 0x7805D4,
 * 0x9C3F81, 0x722B0E). slot+0x24 = status (2 = empty slot, see IsValid 0x415FE0),
 * slot+0x18 = XTEA-encrypted 64-bit vehicle id; its low dword is the id the dealer uses
 * (decrypted and matched against the vehicle catalogue in 0x7808F3..0x780930). */
#define GARAGE_FIRST     0x20
#define GARAGE_STRIDE    0x80
#define GARAGE_SLOTS     0x280
#define SLOT_ENC_ID      0x18
#define SLOT_STATUS      0x24
#define SLOT_EMPTY       2

#define CTX_NEXT         0xAB0
#define CTX_DICT         0x060
#define CTX_DICT_SIZE    0x064

#define DEF_ID           0x08
#define DEF_COLOR        0x20
#define DEF_VARNAME      0x32

#define ANCHOR_WINDOW    0x20          /* /:Current_car fields of "infos" sit next to the anchor */

/* ------------------------------------------------------------------ state ----------------- */
static HMODULE g_self;
static char    g_iniPath[260];
static char    g_logPath[260];
static int     g_log;
static unsigned char g_green[3] = { 0x33, 0xDD, 0x33 };

unsigned g_origSelect = TARGET_SELECT;  /* read by the naked hook */

typedef struct { unsigned char* def; unsigned char rgb[3]; } Saved;
#define MAX_SAVED 64
static Saved g_saved[MAX_SAVED];
static int   g_nSaved;

/* ------------------------------------------------------------------ tiny helpers ---------- */
static int StrLen(const char* s) { int n = 0; while (s[n]) ++n; return n; }
static char* StrCpy(char* d, const char* s) { while ((*d = *s++) != 0) ++d; return d; }
static char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int StrIEqN(const char* a, const char* b, int max)
{
    int i;
    for (i = 0; i < max; ++i)
    {
        if (Lower(a[i]) != Lower(b[i])) return 0;
        if (!b[i]) return 1;
    }
    return 0;
}
static char* UToA(char* d, unsigned v)
{
    char tmp[12]; int n = 0;
    do { tmp[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *d++ = tmp[--n];
    *d = 0;
    return d;
}
static char* HexToA(char* d, unsigned v)
{
    static const char hx[] = "0123456789ABCDEF";
    int i;
    *d++ = '0'; *d++ = 'x';
    for (i = 7; i >= 0; --i) *d++ = hx[(v >> (i * 4)) & 15u];
    *d = 0;
    return d;
}
static int ParseHexRGB(const char* s, unsigned* out)
{
    unsigned v = 0; int n = 0;
    while (*s == ' ' || *s == '\t' || *s == '#') ++s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; ++s, ++n)
    {
        char c = Lower(*s);
        if (c >= '0' && c <= '9') v = v * 16u + (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v = v * 16u + (unsigned)(c - 'a' + 10);
        else break;
    }
    if (n != 6) return 0;
    *out = v;
    return 1;
}

static void Log(const char* msg)
{
    HANDLE h;
    DWORD w;
    if (!g_log) return;
    h = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0x80, 0);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, 0, FILE_END);
    WriteFile(h, msg, (DWORD)StrLen(msg), &w, 0);
    WriteFile(h, "\r\n", 2, &w, 0);
    CloseHandle(h);
}

/* ------------------------------------------------------------------ core logic ------------ */
/* Player profile: thiscall getter [[0x114C15C]]->vtbl[0x24/4](), as used all over the game. */
static unsigned char* GetProfile(void)
{
    unsigned char* r = 0;
    if (!*(void**)G_PROFILE_MGR) return 0;
    __asm {
        xor  edx, edx                         /* tell the compiler EDX is clobbered */
        mov  ecx, dword ptr ds:[G_PROFILE_MGR]
        mov  eax, [ecx]
        call dword ptr [eax + 0x24]
        mov  r, eax
    }
    return r;
}

/* XTEA decipher, identical to the game's 0x6DDD70 (key = 4 dwords at 0xF99F28). */
static unsigned DecryptId(const unsigned* src)
{
    const unsigned* key = (const unsigned*)G_XTEA_KEY;
    unsigned v0 = src[0], v1 = src[1], sum = 0xC6EF3720u;
    int i;
    for (i = 0; i < 32; ++i)
    {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3u]);
        sum += 0x61C88647u;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3u]);
    }
    return v0;
}

static int g_lastSlots;   /* for the log */

static int IsOwned(int carId)
{
    unsigned char* prof;
    int i;
    g_lastSlots = 0;
    if (carId == -1 || carId == 0) return 0;
    prof = GetProfile();
    if (!prof) return 0;
    for (i = 0; i < GARAGE_SLOTS; ++i)
    {
        unsigned char* slot = prof + GARAGE_FIRST + i * GARAGE_STRIDE;
        if (slot[SLOT_STATUS] == SLOT_EMPTY) continue;
        ++g_lastSlots;
        if ((int)DecryptId((const unsigned*)(slot + SLOT_ENC_ID)) == carId) return 1;
    }
    return 0;
}

static int IsEditText(const unsigned char* def, const char* var)
{
    return def && *(const unsigned*)def == VT_EDITTEXT && StrIEqN((const char*)def + DEF_VARNAME, var, 32);
}

static Saved* SavedFor(unsigned char* def)
{
    int i;
    for (i = 0; i < g_nSaved; ++i) if (g_saved[i].def == def) return &g_saved[i];
    if (g_nSaved >= MAX_SAVED) return 0;
    g_saved[g_nSaved].def = def;
    g_saved[g_nSaved].rgb[0] = def[DEF_COLOR + 0];
    g_saved[g_nSaved].rgb[1] = def[DEF_COLOR + 1];
    g_saved[g_nSaved].rgb[2] = def[DEF_COLOR + 2];
    return &g_saved[g_nSaved++];
}

/* Recolour the "infos" /:Current_car fields of every loaded dealer movie. Returns how many. */
static int Recolor(int owned)
{
    unsigned char* ctx = *(unsigned char**)G_FLASH_CTX_HEAD;
    int guard, done = 0;
    for (guard = 0; ctx && guard < 256; ++guard, ctx = *(unsigned char**)(ctx + CTX_NEXT))
    {
        unsigned char** dict = *(unsigned char***)(ctx + CTX_DICT);
        int n = *(int*)(ctx + CTX_DICT_SIZE);
        int id, anchor = -1, lo, hi;
        if (!dict || n <= 0 || n > 0x10000) continue;

        /* Only the dealer movie has "/:m_OutOfStock"; the name fields of the "infos" clip are
         * defined right next to it (0x49/0x4D/0x55/0x59 around 0x54 in both Hi- and LowRes). */
        for (id = 0; id < n; ++id)
            if (IsEditText(dict[id], "/:m_OutOfStock")) { anchor = id; break; }
        if (anchor < 0) continue;

        lo = anchor - ANCHOR_WINDOW; if (lo < 0) lo = 0;
        hi = anchor + ANCHOR_WINDOW; if (hi > n - 1) hi = n - 1;
        for (id = lo; id <= hi; ++id)
        {
            unsigned char* def = dict[id];
            Saved* s;
            if (!IsEditText(def, "/:Current_car")) continue;
            if (*(int*)(def + DEF_ID) != id) continue;
            s = SavedFor(def);
            if (!s) continue;
            if (owned)
            {
                def[DEF_COLOR + 0] = g_green[0];
                def[DEF_COLOR + 1] = g_green[1];
                def[DEF_COLOR + 2] = g_green[2];
            }
            else
            {
                def[DEF_COLOR + 0] = s->rgb[0];
                def[DEF_COLOR + 1] = s->rgb[1];
                def[DEF_COLOR + 2] = s->rgb[2];
            }
            ++done;
        }
    }
    return done;
}

void __cdecl OnDealerSelect(int carId)
{
    int owned = IsOwned(carId);
    int n = Recolor(owned);
    if (g_log)
    {
        char line[128], *p;
        p = StrCpy(line, "dealer selection: vehicle id ");
        p = HexToA(p, (unsigned)carId);
        p = StrCpy(p, owned ? " OWNED" : " not owned");
        p = StrCpy(p, " (garage cars: ");
        p = UToA(p, (unsigned)g_lastSlots);
        p = StrCpy(p, "), name fields recoloured: ");
        UToA(p, (unsigned)n);
        Log(line);
    }
}

/* Replaces `call 0x7782A0` at 0x7264AF. ECX = selected vehicle id. The original call takes
 * register arguments (ESI/EDI), so every register and the flags are preserved. */
__declspec(naked) void HookSelect(void)
{
    __asm {
        pushad
        pushfd
        push ecx
        call OnDealerSelect
        add  esp, 4
        popfd
        popad
        jmp  dword ptr [g_origSelect]
    }
}

/* ------------------------------------------------------------------ patching -------------- */
static int PatchCall(unsigned site, unsigned expectedTarget, void* hook, const char* name)
{
    unsigned char* p = (unsigned char*)site;
    char line[160], *q;
    DWORD old;
    int rel;
    if (p[0] != 0xE8 || (unsigned)(site + 5 + *(int*)(p + 1)) != expectedTarget)
    {
        q = StrCpy(line, "SKIP ");
        q = StrCpy(q, name);
        q = StrCpy(q, ": unexpected bytes at ");
        HexToA(q, site);
        Log(line);
        return 0;
    }
    rel = (int)((unsigned)hook - (site + 5));
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int*)(p + 1) = rel;
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    q = StrCpy(line, "OK   ");
    q = StrCpy(q, name);
    q = StrCpy(q, " hooked at ");
    HexToA(q, site);
    Log(line);
    return 1;
}

static void Init(void)
{
    char* dot = 0, *p;
    char buf[32];
    unsigned rgb;

    GetModuleFileNameA(g_self, g_iniPath, sizeof(g_iniPath));
    for (p = g_iniPath; *p; ++p) if (*p == '.') dot = p;
    if (!dot) return;
    StrCpy(g_logPath, g_iniPath);
    StrCpy(dot, ".ini");
    StrCpy(g_logPath + (dot - g_iniPath), ".log");

    if (!GetPrivateProfileIntA("DealerOwnedGreen", "Enabled", 1, g_iniPath)) return;
    g_log = (int)GetPrivateProfileIntA("DealerOwnedGreen", "Log", 0, g_iniPath);
    GetPrivateProfileStringA("DealerOwnedGreen", "Color", "33DD33", buf, sizeof(buf), g_iniPath);
    if (ParseHexRGB(buf, &rgb))
    {
        g_green[0] = (unsigned char)(rgb >> 16);
        g_green[1] = (unsigned char)(rgb >> 8);
        g_green[2] = (unsigned char)rgb;
    }

    if (g_log)
    {
        /* append, so a test session survives restarting the game */
        Log("----------------------------------------------------------------");
        {
            char line[96];
            p = StrCpy(line, "TDU_DealerOwnedGreen: owned-vehicle colour RGB ");
            HexToA(p, ((unsigned)g_green[0] << 16) | ((unsigned)g_green[1] << 8) | g_green[2]);
            Log(line);
        }
    }

    /* Only touch memory if the host is exactly the supported exe (base 0x400000, PE timestamp
     * 0x468E36BE, SizeOfImage 0x12AF000). */
    {
        unsigned char* base = (unsigned char*)GetModuleHandleA(0);
        unsigned pe;
        if (base != (unsigned char*)0x400000u) { Log("SKIP: host exe base is not 0x400000"); return; }
        pe = *(unsigned*)(base + 0x3C);
        if (*(unsigned*)(base + pe + 8) != 0x468E36BEu || *(unsigned*)(base + pe + 24 + 56) != 0x012AF000u)
        {
            Log("SKIP: unsupported TestDriveUnlimited.exe build (expected 1.66A, timestamp 0x468E36BE)");
            return;
        }
    }

    PatchCall(SITE_SELECT, TARGET_SELECT, (void*)HookSelect, "dealer vehicle selection (0x726110)");
}

BOOL WINAPI DllMain(HMODULE mod, DWORD reason, void* reserved)
{
    (void)reserved;
    if (reason == 1 /* DLL_PROCESS_ATTACH */)
    {
        g_self = mod;
        DisableThreadLibraryCalls(mod);
        Init();
    }
    return 1;
}
