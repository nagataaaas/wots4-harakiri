#include "../src/mod.cpp"
#include <cstdio>
#include <cmath>
namespace {
int passed, failed;
void check(bool condition, const char* name) {
    std::printf("%s: %s\n",condition ? "PASS" : "FAIL",name);
    condition ? ++passed : ++failed;
}
__declspec(naked) void returnFromHook() { __asm { ret } }
BYTE* drainedCharacter;
float drainedAmount, drainedSecondAmount;
uint32_t drainedWithoutArmor;
// Mirrors 0x485820: character in esi, three stack arguments, callee pops them.
__declspec(naked) void mockQueueStaminaDrain() {
    __asm {
        mov drainedCharacter, esi
        mov eax, [esp+4]
        mov drainedAmount, eax
        mov eax, [esp+8]
        mov drainedSecondAmount, eax
        mov eax, [esp+12]
        mov drainedWithoutArmor, eax
        ret 12
    }
}
__declspec(naked) void runHook(void* hook, void* frame) {
    __asm {
        mov eax, [esp+8]
        mov edx, [esp+4]
        push ebp
        mov ebp, eax
        call edx
        pop ebp
        ret
    }
}
__declspec(naked) void runStateHook(void* hook, void* frame, BYTE* state, BYTE* buffer) {
    __asm {
        mov edx, [esp+4]
        mov eax, [esp+8]
        push esi
        push ebx
        mov esi, [esp+20]
        mov ebx, [esp+24]
        push ebp
        mov ebp, eax
        call edx
        pop ebp
        pop ebx
        pop esi
        ret
    }
}
}
int main() {
    originalDamage = originalOutcome = originalKnockout = returnFromHook;
    alignas(16) BYTE stack[128] = {};
    BYTE* frame = stack + 64;
    volatile float& damage = *reinterpret_cast<float*>(frame-8);
    volatile float& remaining = *reinterpret_cast<float*>(frame-0x14);
    alignas(16) BYTE character[0x400] = {};
    *reinterpret_cast<void**>(frame-0xc) = character;
    volatile float& vitality = *reinterpret_cast<float*>(character+vitalityOffset);
    volatile BYTE& poolIncrease = frame[poolIncreaseArgumentOffset];
    BYTE gameState[0x20] = {}, buffer[0x20] = {};
    BYTE* gameStateInstance = gameState;
    gameStatePointer = reinterpret_cast<uintptr_t>(&gameStateInstance);
    gameState[0x18] = 3;
    selectedDifficulty = Difficulty::Normal;
    damage = 25; remaining = 100; vitality = 50;
    runHook(damageHook,frame); runHook(outcomeHook,frame); runHook(knockoutHook,frame);
    check(damage == 25 && remaining == 100 && vitality == 50,"disabled hooks preserve damage and pools");
    selectedDifficulty = Difficulty::Harakiri;
    runHook(damageHook,frame);
    check(damage == 1000000000.0f && std::isfinite(damage),"large finite attack damage");
    check(damage > 99999,"damage exceeds ordinary maximum health");
    remaining = 1;
    runHook(outcomeHook,frame);
    check(remaining == 0,"native survival bonus cannot leave one health point");
    runHook(knockoutHook,frame);
    check(vitality == 0,"knockout vitality is zero");
    damage = 500; remaining = 1; // The native vitality floor clamp rewrites the damage.
    runHook(outcomeHook,frame);
    check(remaining == 1,"slash clamped to the native vitality floor survives");
    poolIncrease = 1; damage = 25; remaining = 100;
    runHook(damageHook,frame); runHook(outcomeHook,frame);
    check(damage == 25 && remaining == 100,"pool increase call keeps native amount and pool");
    poolIncrease = 0;
    damage = 0; remaining = 100;
    runHook(damageHook,frame); runHook(outcomeHook,frame);
    check(damage == 0 && remaining == 100,"natively zeroed damage stays harmless");
    meleeHitReturn = 0x12345678;
    queueStaminaDrainFunction = mockQueueStaminaDrain;
    uint32_t hitRecord[6] = {0xe0,0xffffffff,arrestKickKind,0,3,1};
    *reinterpret_cast<uintptr_t*>(frame+4) = meleeHitReturn;
    *reinterpret_cast<uint32_t**>(frame+hitRecordArgumentOffset) = hitRecord;
    *reinterpret_cast<float*>(character+staminaMaxOffset) = 1000;
    *reinterpret_cast<float*>(character+staminaShieldOffset) = 20;
    damage = 40; remaining = 1000;
    runHook(damageHook,frame); runHook(outcomeHook,frame);
    check(damage == 40 && remaining == 1000,"arrest kick keeps its native health damage");
    check(drainedCharacter == character && drainedAmount == 1020 && drainedSecondAmount == 0 && drainedWithoutArmor == 1,
        "arrest kick empties stamina and shield without armor reduction");
    drainedCharacter = nullptr;
    *reinterpret_cast<int*>(character+characterIdOffset) = 215;
    damage = 40;
    runHook(damageHook,frame);
    check(damage == 1000000000.0f && !drainedCharacter,"kick against an NPC stays lethal");
    *reinterpret_cast<int*>(character+characterIdOffset) = playerCharacterId;
    hitRecord[2] = 0x27;
    damage = 40;
    runHook(damageHook,frame);
    check(damage == 1000000000.0f && !drainedCharacter,"other melee hits on the player stay lethal");
    hitRecord[2] = arrestKickKind;
    *reinterpret_cast<uintptr_t*>(frame+4) = 0;
    damage = 40;
    runHook(damageHook,frame);
    check(damage == 1000000000.0f && !drainedCharacter,"hits from other callers stay lethal");
    *reinterpret_cast<uint32_t**>(frame+hitRecordArgumentOffset) = nullptr;
    gameState[0x18] = 1;
    damage = 25; remaining = 100; vitality = 50;
    runHook(damageHook,frame); runHook(outcomeHook,frame); runHook(knockoutHook,frame);
    check(damage == 25 && remaining == 100 && vitality == 50,"new cycle native reset prevents stale Harakiri effects");
    gameState[0x18] = 4;
    restoreDifficulty(gameState);
    check(selectedDifficulty == Difficulty::Harakiri && gameState[0x18] == 3,"Harakiri restores with native Very Hard behavior");
    serializeDifficulty(gameState,buffer);
    check(buffer[0] == 4 && gameState[0x18] == 3,"save buffer retains Harakiri enum without changing native state");
    gameState[0x18] = buffer[0];
    restoreDifficulty(gameState);
    check(selectedDifficulty == Difficulty::Harakiri,"death snapshot restores Harakiri enum");
    gameState[0x18] = 3;
    restoreDifficulty(gameState);
    damage = 25;
    runHook(damageHook,frame);
    check(damage == 25 && selectedDifficulty == Difficulty::VeryHard,"existing Very Hard save does not enable lethal attacks");
    gameState[0x18] = 1;
    restoreDifficulty(gameState);
    check(selectedDifficulty == Difficulty::Normal,"loading Normal clears the previous Harakiri mode");
    for(BYTE difficulty = 0; difficulty < 4; ++difficulty) {
        gameState[0x18] = difficulty;
        restoreDifficulty(gameState);
        serializeDifficulty(gameState,buffer);
        check(buffer[0] == difficulty && gameState[0x18] == difficulty,"native difficulty round trip preserves enum");
    }
    originalRestore = originalSerialize = returnFromHook;
    gameState[0x18] = 4;
    runStateHook(restoreHook,frame,gameState,buffer);
    check(gameState[0x18] == 3 && selectedDifficulty == Difficulty::Harakiri,"restore bridge normalizes before native consumers");
    *reinterpret_cast<BYTE**>(frame+8) = gameState;
    runStateHook(serializeHook,frame,gameState,buffer);
    check(buffer[0] == 4 && gameState[0x18] == 3,"serialize bridge receives native state and destination buffer");
    gameState[0x18] = 1;
    runStateHook(serializeHook,frame,gameState,buffer);
    check(buffer[0] == 1,"cycle completion cannot save a stale Harakiri enum");
    check(ModInitialize(nullptr) == FALSE,"missing context is refused");
    wots4::ModContext context{};
    check(ModInitialize(&context) == FALSE,"incomplete context is refused");
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr,supportedImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image) return 1;
    gameBase = reinterpret_cast<uintptr_t>(image);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 0x80;
    auto* pe = reinterpret_cast<IMAGE_NT_HEADERS32*>(image+0x80);
    pe->Signature = IMAGE_NT_SIGNATURE;
    pe->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    pe->FileHeader.TimeDateStamp = supportedTimestamp;
    pe->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    pe->OptionalHeader.SizeOfImage = supportedImageSize;
    for(const auto& hook : hooks)
        std::memcpy(reinterpret_cast<void*>(relocated(hook.address)),hook.expected,hook.length);
    *reinterpret_cast<uintptr_t*>(relocated(menuSelectAddress)+5) = relocated(gameStateAddress);
    std::memcpy(reinterpret_cast<void*>(relocated(appendRowAddress)),appendRowBytes,sizeof(appendRowBytes));
    std::memcpy(reinterpret_cast<void*>(relocated(clearRowsAddress)),clearRowsBytes,sizeof(clearRowsBytes));
    std::memcpy(reinterpret_cast<void*>(relocated(queueStaminaDrainAddress)),queueStaminaDrainBytes,sizeof(queueStaminaDrainBytes));
    auto* meleeHitCall = reinterpret_cast<BYTE*>(relocated(meleeHitCallAddress));
    meleeHitCall[0] = 0xe8;
    *reinterpret_cast<int32_t*>(meleeHitCall+1) = static_cast<int32_t>(damageFunctionAddress-meleeHitReturnAddress);
    check(supportedImage(),"supported synthetic PE at relocated base");
    meleeHitCall[1] ^= 1;
    check(!supportedImage(),"melee hit call to another function is refused");
    meleeHitCall[1] ^= 1;
    image[damageAddress-preferredImageBase] ^= 1;
    check(!supportedImage(),"changed damage code is refused");
    image[damageAddress-preferredImageBase] ^= 1;
    ++pe->FileHeader.TimeDateStamp;
    check(!supportedImage(),"changed timestamp is refused");
    --pe->FileHeader.TimeDateStamp;
    pe->OptionalHeader.SizeOfImage = steamImageSize;
    check(supportedImage(),"running Steam executable image size is accepted");
    ++pe->OptionalHeader.SizeOfImage;
    check(!supportedImage(),"unknown image size is refused");
    pe->OptionalHeader.SizeOfImage = supportedImageSize;
    pe->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    check(!supportedImage(),"64-bit image is refused");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%d checks passed, %d failed. No game installation, saves, or game process were written.\n",passed,failed);
    return failed ? 1 : 0;
}
