#include <wots4/mod_api.h>
#include <MinHook.h>
#include <array>
#include <cstring>
#include <cwchar>

namespace {
constexpr uintptr_t preferredImageBase = 0x400000;
constexpr DWORD supportedTimestamp = 0x683d42eb;
constexpr DWORD supportedImageSize = 0x65b000;
// The Steam executable keeps its DRM wrapper section, so the running image is larger than the decrypted copy.
constexpr DWORD steamImageSize = 0x689000;
// Shared attack damage before pool subtraction.
constexpr uintptr_t damageAddress = 0x49db35;
// Remaining pool before the native death/knockout decision.
constexpr uintptr_t outcomeAddress = 0x49de4b;
// Native knockout refills vitality; Harakiri leaves it at zero.
constexpr uintptr_t knockoutAddress = 0x49dee2;
enum : DWORD { lethalDamageBits = 0x4e6e6b28 }; // 1,000,000,000.0f: finite and within signed integer range.
enum : size_t { vitalityOffset = 0x1f8 }; // Current vitality in CharacterParameter.
// Damage function frame: nonzero third argument adds the amount to the pool instead of subtracting it.
enum : size_t { poolIncreaseArgumentOffset = 0x14 };
// Constables' arrest kicks reach the damage function from the melee hit handler (return address 0x488a5a),
// whose fifth argument is its hit record; the record's third field is 0x28 for these kicks.
constexpr uintptr_t meleeHitReturnAddress = 0x488a5a;
enum : size_t { hitRecordArgumentOffset = 0x18, hitRecordKindOffset = 8, characterIdOffset = 4 };
enum : DWORD { arrestKickKind = 0x28 };
enum : int { playerCharacterId = 0 };
// 0x485820 queues a stamina drain on the character in esi and raises the exhaustion event when it empties
// stamina; a nonzero third argument skips the armor reduction. Exhaustion is what lets constables arrest.
constexpr uintptr_t queueStaminaDrainAddress = 0x485820;
enum : size_t { staminaMaxOffset = 0x34c, staminaShieldOffset = 0x354 };
uintptr_t meleeHitReturn;
void* queueStaminaDrainFunction;
uintptr_t gameBase;
uintptr_t relocated(uintptr_t address) { return gameBase + address - preferredImageBase; }
enum class Difficulty : BYTE { Easy, Normal, Hard, VeryHard, Harakiri };
enum : BYTE { veryHardDifficulty = 3, harakiriDifficulty = 4 };
Difficulty selectedDifficulty = Difficulty::Normal;
BYTE effectiveDifficulty(BYTE difficulty) {
    return difficulty == harakiriDifficulty ? veryHardDifficulty : difficulty;
}
BYTE savedDifficulty(BYTE nativeDifficulty) {
    return selectedDifficulty == Difficulty::Harakiri && nativeDifficulty == veryHardDifficulty
        ? harakiriDifficulty : nativeDifficulty;
}
enum : size_t { nativeDifficultyOffset = 0x18 }; // Game state stores its effective difficulty here.
void restoreDifficulty(BYTE* gameState) {
    selectedDifficulty = static_cast<Difficulty>(gameState[0x18]);
    gameState[0x18] = effectiveDifficulty(gameState[0x18]);
}
void serializeDifficulty(BYTE* gameState, BYTE* buffer) {
    buffer[0] = savedDifficulty(gameState[0x18]);
}
void* originalDamage;
void* originalOutcome;
void* originalKnockout;
// Difficulty list finalization and selection; the native mapping holds four entries.
constexpr uintptr_t menuBuildAddress = 0x4ed82e;
constexpr uintptr_t menuSelectAddress = 0x4ed5d1;
constexpr uintptr_t appendRowAddress = 0x5bdb20;
constexpr uintptr_t clearRowsAddress = 0x5bda60; // Clears menu text and selectable rows before layout.
constexpr uintptr_t gameStateAddress = 0x9e967c;
constexpr uintptr_t textManagerAddress = 0x9e9680;
// Normalize immediately after the native bulk copy, before consumers see the new state.
constexpr uintptr_t restoreAddress = 0x4d9f88;
constexpr uintptr_t serializeAddress = 0x4d9efa;
// Save list uses a four-entry stack table; intercept before indexing it.
constexpr uintptr_t saveLabelAddress = 0x53fe4b;
constexpr size_t difficultyMapOffset = 0x2a;
// Selectable-row flags of the item column (column 1); 0x5bdb20 addresses column N at 0x40 + N*0x30.
enum : size_t { menuFlagsBeginOffset = 0x70, menuFlagsEndOffset = 0x74 };
constexpr uint32_t nativeDifficultyCount = 4;
void* originalMenuBuild;
void* originalMenuSelect;
void* originalRestore;
void* originalSerialize;
void* originalSaveLabel;
void* appendRowFunction;
void (__stdcall* clearRowsFunction)(void*);
uintptr_t selectionContinuation;
uintptr_t gameStatePointer;
uintptr_t saveLabelContinuation;
BYTE* difficultyScreen;
int harakiriRow = -1;

const wchar_t* modeLabel(const wchar_t* nativeEasyLabel) {
    return nativeEasyLabel && nativeEasyLabel[0] > 0x7f ? L"一撃死" : L"Harakiri";
}

const wchar_t* nativeDifficultyLabel(BYTE difficulty) {
    static const wchar_t* fallback[] = {L"Easy",L"Normal",L"Hard",L"VeryHard"};
    if(difficulty >= nativeDifficultyCount) return L"Unknown";
    auto* manager = *reinterpret_cast<BYTE**>(relocated(textManagerAddress));
    if(!manager) return fallback[difficulty];
    auto* mainText = *reinterpret_cast<BYTE**>(manager+8);
    constexpr uint32_t easyLabelId = 163;
    const auto textId = easyLabelId+difficulty;
    if(!mainText || *reinterpret_cast<uint32_t*>(mainText) <= textId) return fallback[difficulty];
    auto** labels = *reinterpret_cast<const wchar_t***>(mainText+4);
    const auto* label = labels ? labels[textId] : nullptr;
    if(!label) return fallback[difficulty];
    // Native English uses Harakiri for this entry; reserve that name for the new mode.
    if(difficulty == veryHardDifficulty && label[0] <= 0x7f) return L"VeryHard";
    return label;
}

// The existing translated label identifies the active text language.
const wchar_t* localizedModeLabel() {
    return modeLabel(nativeDifficultyLabel(0));
}

__declspec(naked) void __cdecl appendRow(void* menu, const wchar_t* label) {
    __asm {
        push edi
        mov edi, [esp+8]
        mov ecx, [esp+12]
        mov edx, 1
        push 1
        call dword ptr [appendRowFunction]
        pop edi
        ret
    }
}

bool appendModeRow(BYTE* screen, BYTE* menu, const wchar_t* label) {
    difficultyScreen = nullptr;
    harakiriRow = -1;
    auto* begin = *reinterpret_cast<uint32_t**>(menu+menuFlagsBeginOffset);
    auto* end = *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset);
    const uintptr_t beginAddress = reinterpret_cast<uintptr_t>(begin);
    const uintptr_t endAddress = reinterpret_cast<uintptr_t>(end);
    if(!begin || endAddress < beginAddress || (endAddress-beginAddress)%sizeof(uint32_t)) return false;
    const size_t count = (endAddress-beginAddress)/sizeof(uint32_t);
    if(count < 2 || count > nativeDifficultyCount) return false;
    const auto* mapping = reinterpret_cast<const uint16_t*>(screen+difficultyMapOffset);
    for(size_t index = 0; index < count; ++index)
        if(mapping[index] >= nativeDifficultyCount) return false;
    clearRowsFunction(menu);
    for(size_t index = 0; index < count; ++index)
        appendRow(menu,nativeDifficultyLabel(static_cast<BYTE>(mapping[index])));
    appendRow(menu,label);
    begin = *reinterpret_cast<uint32_t**>(menu+menuFlagsBeginOffset);
    end = *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset);
    if(reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(begin) != (count+1)*sizeof(uint32_t))
        return false;
    difficultyScreen = screen;
    harakiriRow = static_cast<int>(count);
    return true;
}

void __cdecl buildModeRow(BYTE* screen, BYTE* menu) {
    appendModeRow(screen,menu,localizedModeLabel());
}

BYTE __cdecl selectDifficulty(BYTE* screen, int selectedRow) {
    if(screen == difficultyScreen && selectedRow == harakiriRow && harakiriRow >= 2) {
        selectedDifficulty = Difficulty::Harakiri;
        return veryHardDifficulty;
    }
    if(selectedRow < 0 || selectedRow >= static_cast<int>(nativeDifficultyCount)) {
        selectedDifficulty = Difficulty::Normal;
        return 1;
    }
    const auto* mapping = reinterpret_cast<const uint16_t*>(screen+difficultyMapOffset);
    const BYTE difficulty = mapping[selectedRow] < nativeDifficultyCount ? static_cast<BYTE>(mapping[selectedRow]) : 1;
    selectedDifficulty = static_cast<Difficulty>(difficulty);
    return difficulty;
}

__declspec(naked) void menuBuildHook() {
    __asm {
        pushfd
        pushad
        push esi
        push ebx
        call buildModeRow
        add esp, 8
        popad
        popfd
        jmp dword ptr [originalMenuBuild]
    }
}

__declspec(naked) void menuSelectHook() {
    __asm {
        pushfd
        pushad
        push eax
        push ebx
        call selectDifficulty
        add esp, 8
        mov [esp+20], eax // Saved EDX supplies DL to the native difficulty store.
        popad
        popfd
        mov eax, gameStatePointer
        mov eax, [eax]
        jmp dword ptr [selectionContinuation]
    }
}

__declspec(naked) void restoreHook() {
    __asm {
        pushfd
        pushad
        push esi
        call restoreDifficulty
        add esp, 4
        popad
        popfd
        jmp dword ptr [originalRestore]
    }
}
__declspec(naked) void serializeHook() {
    __asm {
        pushfd
        pushad
        push ebx
        push dword ptr [ebp+8]
        call serializeDifficulty
        add esp, 8
        popad
        popfd
        jmp dword ptr [originalSerialize]
    }
}
const wchar_t* saveDifficultyLabel(BYTE difficulty) {
    if(difficulty == harakiriDifficulty) return localizedModeLabel();
    return difficulty == veryHardDifficulty ? nativeDifficultyLabel(difficulty) : L"Unknown";
}
__declspec(naked) void saveLabelHook() {
    __asm {
        pushfd
        cmp ecx, veryHardDifficulty
        jae customLabel
        popfd
        jmp dword ptr [originalSaveLabel]
    customLabel:
        pushad
        push ecx
        call saveDifficultyLabel
        add esp, 4
        mov [esp+24], eax
        popad
        popfd
        jmp dword ptr [saveLabelContinuation]
    }
}
// Only arrest kicks reach this; the native damage code calls game functions here with an empty x87 stack too.
void __cdecl exhaustStamina(BYTE* character) {
    const float drain = *reinterpret_cast<const float*>(character+staminaMaxOffset) +
        *reinterpret_cast<const float*>(character+staminaShieldOffset);
    __asm {
        mov esi, character
        push 1
        push 0
        push dword ptr [drain]
        call dword ptr [queueStaminaDrainFunction]
    }
}
// Integer stores preserve the x87 stack and avoid calls in the attack path.
__declspec(naked) void damageHook() {
    __asm {
        pushfd
        cmp selectedDifficulty, harakiriDifficulty
        jne finished
        push eax
        mov eax, gameStatePointer
        mov eax, [eax]
        test eax, eax
        je noDamageChange
        cmp byte ptr [eax+nativeDifficultyOffset], veryHardDifficulty
        jne noDamageChange
        cmp byte ptr [ebp+poolIncreaseArgumentOffset], 0
        jne noDamageChange
        // Native code zeroes damage for protected targets (0x49dac8, 0x49db03, 0x49db30); keep those harmless.
        cmp dword ptr [ebp-8], 0 // Positive floats compare as positive integers.
        jle noDamageChange
        // Arrest kicks keep their native health damage and empty stamina instead, so one kick arrests.
        mov eax, dword ptr [ebp+4]
        cmp eax, meleeHitReturn
        jne lethalDamage
        mov eax, dword ptr [ebp+hitRecordArgumentOffset]
        test eax, eax
        je lethalDamage
        cmp dword ptr [eax+hitRecordKindOffset], arrestKickKind
        jne lethalDamage
        mov eax, dword ptr [ebp-0ch]
        cmp dword ptr [eax+characterIdOffset], playerCharacterId
        jne lethalDamage
        pop eax
        popfd
        pushad
        pushfd
        push dword ptr [ebp-0ch]
        call exhaustStamina
        add esp, 4
        popfd
        popad
        jmp dword ptr [originalDamage]
    lethalDamage:
        mov dword ptr [ebp-8], lethalDamageBits
    noDamageChange:
        pop eax
    finished:
        popfd
        jmp dword ptr [originalDamage]
    }
}
__declspec(naked) void outcomeHook() {
    __asm {
        pushfd
        cmp selectedDifficulty, harakiriDifficulty
        jne finished
        push eax
        mov eax, gameStatePointer
        mov eax, [eax]
        test eax, eax
        je noOutcomeChange
        cmp byte ptr [eax+nativeDifficultyOffset], veryHardDifficulty
        jne noOutcomeChange
        // Only hits that still carry the lethal damage: zero-damage hits, pool increases, and slashes
        // clamped to a vitality floor (0x49dd1f rewrites the damage), e.g. arrests, keep the native result.
        cmp dword ptr [ebp-8], lethalDamageBits
        jne noOutcomeChange
        // Override the native random survival bonus after pool subtraction.
        mov dword ptr [ebp-14h], 0
    noOutcomeChange:
        pop eax
    finished:
        popfd
        jmp dword ptr [originalOutcome]
    }
}
__declspec(naked) void knockoutHook() {
    __asm {
        pushfd
        cmp selectedDifficulty, harakiriDifficulty
        jne finished
        push eax
        mov eax, gameStatePointer
        mov eax, [eax]
        test eax, eax
        je noKnockoutChange
        cmp byte ptr [eax+nativeDifficultyOffset], veryHardDifficulty
        jne noKnockoutChange
        mov eax, dword ptr [ebp-0ch]
        mov dword ptr [eax+vitalityOffset], 0
    noKnockoutChange:
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
const BYTE menuBuildBytes[] = {0x8b,0xc6,0xe8,0xbb,0x05,0x0d,0x00};
const BYTE menuSelectBytes[] = {0x8a,0x54,0x43,0x2a,0xa1,0x7c,0x96,0x9e,0x00};
const BYTE appendRowBytes[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x6a,0xff};
const BYTE clearRowsBytes[] = {0x55,0x8b,0xec,0x51,0x53};
const BYTE queueStaminaDrainBytes[] = {0x55,0x8b,0xec,0x80,0x7d,0x10,0x00};
constexpr uintptr_t meleeHitCallAddress = 0x488a55; // call 0x49d3e0, returning to meleeHitReturnAddress
constexpr uintptr_t damageFunctionAddress = 0x49d3e0;
const BYTE restoreBytes[] = {0x83,0xc4,0x0c,0x80,0x7d,0x10,0x00};
const BYTE serializeBytes[] = {0x8b,0x45,0x08,0x8b,0x08};
const BYTE saveLabelBytes[] = {0x8b,0x4c,0x8d,0xbc,0x3b,0x08};
const std::array<HookSpec,8> hooks = {{
    {damageAddress,damageBytes,sizeof(damageBytes),damageHook,&originalDamage},
    {outcomeAddress,outcomeBytes,sizeof(outcomeBytes),outcomeHook,&originalOutcome},
    {knockoutAddress,knockoutBytes,sizeof(knockoutBytes),knockoutHook,&originalKnockout},
    {menuBuildAddress,menuBuildBytes,sizeof(menuBuildBytes),menuBuildHook,&originalMenuBuild},
    {menuSelectAddress,menuSelectBytes,sizeof(menuSelectBytes),menuSelectHook,&originalMenuSelect},
    {restoreAddress,restoreBytes,sizeof(restoreBytes),restoreHook,&originalRestore},
    {serializeAddress,serializeBytes,sizeof(serializeBytes),serializeHook,&originalSerialize},
    {saveLabelAddress,saveLabelBytes,sizeof(saveLabelBytes),saveLabelHook,&originalSaveLabel}
}};
bool supportedImage() {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(gameBase);
    if(dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
        return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(gameBase + dos->e_lfanew);
    if(pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        pe->FileHeader.TimeDateStamp != supportedTimestamp)
        return false;
    const auto imageSize = pe->OptionalHeader.SizeOfImage;
    if(imageSize != supportedImageSize && imageSize != steamImageSize) return false;
    for(const auto& hook : hooks) {
        // The selection instruction contains an absolute address relocated by Windows.
        const auto comparedLength = hook.address == menuSelectAddress ? 5 : hook.length;
        if(std::memcmp(reinterpret_cast<const void*>(relocated(hook.address)),hook.expected,comparedLength))
            return false;
    }
    if(*reinterpret_cast<const uintptr_t*>(relocated(menuSelectAddress)+5) != relocated(gameStateAddress) ||
        std::memcmp(reinterpret_cast<void*>(relocated(appendRowAddress)),appendRowBytes,sizeof(appendRowBytes)) ||
        std::memcmp(reinterpret_cast<void*>(relocated(clearRowsAddress)),clearRowsBytes,sizeof(clearRowsBytes)) ||
        std::memcmp(reinterpret_cast<void*>(relocated(queueStaminaDrainAddress)),queueStaminaDrainBytes,sizeof(queueStaminaDrainBytes)))
        return false;
    constexpr BYTE callOpcode = 0xe8;
    const auto* meleeHitCall = reinterpret_cast<const BYTE*>(relocated(meleeHitCallAddress));
    const auto callDisplacement = *reinterpret_cast<const int32_t*>(meleeHitCall+1);
    return meleeHitCall[0] == callOpcode &&
        meleeHitCallAddress+5+callDisplacement == damageFunctionAddress && meleeHitCallAddress+5 == meleeHitReturnAddress;
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
    if(!context || context->size < sizeof(*context) || !context->game || !context->directory || !context->host)
        return FALSE;
    if(context->version != wots4::modApiVersion || context->host->size < sizeof(wots4::HostApi) ||
        context->host->version != wots4::modApiVersion || !context->host->claim || !context->host->releaseClaims) {
        if(context->host->size >= sizeof(uint32_t)*2+sizeof(void*) && context->host->log)
            context->host->log(context->module,
                "[ERROR] Could not initialize Harakiri: the ModLoader's Mod API version differs from the one this Mod was built for; initialization stopped.");
        return FALSE;
    }
    gameBase = reinterpret_cast<uintptr_t>(context->game);
    if(!supportedImage()) {
        log(*context,"[ERROR] Could not initialize Harakiri: game version or code does not match; initialization stopped.");
        return FALSE;
    }
    wchar_t configurationPath[MAX_PATH];
    if(swprintf_s(configurationPath,L"%sconfig.ini",context->directory) < 0) return FALSE;
    if(!GetPrivateProfileIntW(L"harakiri",L"enabled",1,configurationPath)) {
        log(*context,"[INFO] Harakiri is disabled in config.ini; initialization finished without installing hooks.");
        return TRUE;
    }
    appendRowFunction = reinterpret_cast<void*>(relocated(appendRowAddress));
    clearRowsFunction = reinterpret_cast<void (__stdcall*)(void*)>(relocated(clearRowsAddress));
    selectionContinuation = relocated(menuSelectAddress)+sizeof(menuSelectBytes);
    gameStatePointer = relocated(gameStateAddress);
    saveLabelContinuation = relocated(0x53fe60);
    meleeHitReturn = relocated(meleeHitReturnAddress);
    queueStaminaDrainFunction = reinterpret_cast<void*>(relocated(queueStaminaDrainAddress));
    if(!installHooks(*context)) return FALSE;
    log(*context,"[INFO] Harakiri initialized; the difficulty menu includes one-hit death mode for the player and NPCs.");
    return TRUE;
}
