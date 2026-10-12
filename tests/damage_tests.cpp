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
    modeEnabled = 0;
    damage = 25; remaining = 100; vitality = 50;
    runHook(damageHook,frame); runHook(outcomeHook,frame); runHook(knockoutHook,frame);
    check(damage == 25 && remaining == 100 && vitality == 50,"disabled hooks preserve damage and pools");
    modeEnabled = 1;
    runHook(damageHook,frame);
    check(damage == 1000000000.0f && std::isfinite(damage),"large finite attack damage");
    check(damage > 99999,"damage exceeds ordinary maximum health");
    remaining = 1;
    runHook(outcomeHook,frame);
    check(remaining == 0,"native survival bonus cannot leave one health point");
    runHook(knockoutHook,frame);
    check(vitality == 0,"knockout vitality is zero");
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
    check(supportedImage(),"supported synthetic PE at relocated base");
    image[damageAddress-preferredImageBase] ^= 1;
    check(!supportedImage(),"changed damage code is refused");
    image[damageAddress-preferredImageBase] ^= 1;
    ++pe->FileHeader.TimeDateStamp;
    check(!supportedImage(),"changed timestamp is refused");
    --pe->FileHeader.TimeDateStamp;
    pe->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    check(!supportedImage(),"64-bit image is refused");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%d checks passed, %d failed. No game installation, saves, or game process were written.\n",passed,failed);
    return failed ? 1 : 0;
}
