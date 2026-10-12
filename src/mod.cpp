#include <wots4/mod_api.h>
#include <MinHook.h>
#include <array>
#include <cstring>
#include <cwchar>

namespace {
constexpr uintptr_t preferredImageBase = 0x400000;
constexpr DWORD supportedTimestamp = 0x683d42eb;
constexpr DWORD supportedImageSize = 0x65b000;
// Shared attack damage before pool subtraction.
constexpr uintptr_t damageAddress = 0x49db35;
// Remaining pool before the native death/knockout decision.
constexpr uintptr_t outcomeAddress = 0x49de4b;
// Native knockout refills vitality; Harakiri leaves it at zero.
constexpr uintptr_t knockoutAddress = 0x49dee2;
enum : DWORD { lethalDamageBits = 0x4e6e6b28 }; // 1,000,000,000.0f: finite and within signed integer range.
enum : size_t { vitalityOffset = 0x1f8 }; // Current vitality in CharacterParameter.
uintptr_t gameBase;
BYTE modeEnabled;
void* originalDamage;
void* originalOutcome;
void* originalKnockout;
uintptr_t relocated(uintptr_t address) { return gameBase + address - preferredImageBase; }

// Integer stores preserve the x87 stack and avoid calls in the attack path.
__declspec(naked) void damageHook() {
    __asm {
        pushfd
        cmp modeEnabled, 0
        je finished
        mov dword ptr [ebp-8], lethalDamageBits
    finished:
        popfd
        jmp dword ptr [originalDamage]
    }
}
__declspec(naked) void outcomeHook() {
    __asm {
        pushfd
        cmp modeEnabled, 0
        je finished
        // Override the native random survival bonus after pool subtraction.
        mov dword ptr [ebp-14h], 0
    finished:
        popfd
        jmp dword ptr [originalOutcome]
    }
}
__declspec(naked) void knockoutHook() {
    __asm {
        pushfd
        cmp modeEnabled, 0
        je finished
        push eax
        mov eax, dword ptr [ebp-0ch]
        mov dword ptr [eax+vitalityOffset], 0
        pop eax
    finished:
        popfd
        jmp dword ptr [originalKnockout]
    }
}
struct HookSpec {
    uintptr_t address;
    const BYTE* expected;
    uint32_t length;
    void* detour;
    void** original;
};
const BYTE damageBytes[] = {0x80,0x7d,0x14,0x00,0x0f,0x85,0x40,0x01,0x00,0x00};
const BYTE outcomeBytes[] = {0xd9,0xee,0x8b,0x55,0xf0};
const BYTE knockoutBytes[] = {0xc7,0x45,0xd8,0x01,0x00,0x00,0x00};
const std::array<HookSpec,3> hooks = {{
    {damageAddress,damageBytes,sizeof(damageBytes),damageHook,&originalDamage},
    {outcomeAddress,outcomeBytes,sizeof(outcomeBytes),outcomeHook,&originalOutcome},
    {knockoutAddress,knockoutBytes,sizeof(knockoutBytes),knockoutHook,&originalKnockout}
}};
bool supportedImage() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameBase);
    if(dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(gameBase + dos->e_lfanew);
    if(pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->FileHeader.TimeDateStamp != supportedTimestamp || pe->OptionalHeader.SizeOfImage != supportedImageSize)
        return false;
    for(const auto& hook : hooks)
        if(std::memcmp(reinterpret_cast<const void*>(relocated(hook.address)),hook.expected,hook.length))
            return false;
    return true;
}
void log(const wots4::ModContext& context, const char* message) {
    if(context.host->log) context.host->log(context.module,message);
}
bool installHooks(const wots4::ModContext& context) {
    for(const auto& hook : hooks) {
        if(!context.host->claim(context.module,relocated(hook.address),hook.length)) {
            context.host->releaseClaims(context.module);
            log(context,"[ERROR] Could not reserve Harakiri hooks: a range is owned by another Mod; initialization stopped.");
            return false;
        }
    }
    const auto initialized = MH_Initialize();
    if(initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) {
        context.host->releaseClaims(context.module);
        log(context,"[ERROR] Could not initialize Harakiri hooks: MinHook failed; initialization stopped.");
        return false;
    }
    size_t created = 0, enabled = 0;
    for(const auto& hook : hooks) {
        if(MH_CreateHook(reinterpret_cast<void*>(relocated(hook.address)),hook.detour,hook.original) != MH_OK) break;
        ++created;
    }
    if(created == hooks.size()) {
        for(const auto& hook : hooks) {
            if(MH_EnableHook(reinterpret_cast<void*>(relocated(hook.address))) != MH_OK) break;
            ++enabled;
        }
    }
    if(enabled == hooks.size()) return true;
    for(size_t index = 0; index < enabled; ++index)
        MH_DisableHook(reinterpret_cast<void*>(relocated(hooks[index].address)));
    for(size_t index = 0; index < created; ++index)
        MH_RemoveHook(reinterpret_cast<void*>(relocated(hooks[index].address)));
    context.host->releaseClaims(context.module);
    log(context,"[ERROR] Could not install Harakiri hooks: creation or activation failed; installed hooks were rolled back.");
    return false;
}
} // namespace

extern "C" BOOL WINAPI ModInitialize(const wots4::ModContext* context) {
    if(!context || context->size < sizeof(*context) || context->version != wots4::modApiVersion ||
        !context->game || !context->directory || !context->host ||
        context->host->size < sizeof(wots4::HostApi) || context->host->version != wots4::modApiVersion ||
        !context->host->claim || !context->host->releaseClaims)
        return FALSE;
    gameBase = reinterpret_cast<uintptr_t>(context->game);
    if(!supportedImage()) {
        log(*context,"[ERROR] Could not initialize Harakiri: game version or code does not match; initialization stopped.");
        return FALSE;
    }
    wchar_t configurationPath[MAX_PATH];
    if(swprintf_s(configurationPath,L"%sconfig.ini",context->directory) < 0) return FALSE;
    modeEnabled = GetPrivateProfileIntW(L"harakiri",L"enabled",1,configurationPath) != 0;
    if(!installHooks(*context)) return FALSE;
    log(*context,"[INFO] Harakiri initialized; lethal attacks affect the player and NPCs.");
    return TRUE;
}
