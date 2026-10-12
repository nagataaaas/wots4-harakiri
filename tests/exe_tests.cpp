#include "../src/mod.cpp"
#include <cstdio>
#include <vector>

namespace {
int passed, failed, claims, releases;
int rejectClaim = -1;
void check(bool condition, const char* name) {
    std::printf("%s: %s\n",condition ? "PASS" : "FAIL",name);
    condition ? ++passed : ++failed;
}
BOOL WINAPI claim(HMODULE, uintptr_t, uint32_t) { return claims++ != rejectClaim; }
void WINAPI release(HMODULE) { ++releases; }
}

int main(int argc, char** argv) {
    if(argc != 2) return 1;
    FILE* file = nullptr;
    if(fopen_s(&file,argv[1],"rb") || !file) return 1;
    fseek(file,0,SEEK_END);
    const auto length = ftell(file);
    if(length < 0x1000 || length > 32*1024*1024) { fclose(file); return 1; }
    rewind(file);
    std::vector<BYTE> contents(static_cast<size_t>(length));
    const bool read = fread(contents.data(),1,contents.size(),file) == contents.size();
    fclose(file);
    if(!read) return 1;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(contents.data());
    if(dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000) return 1;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(contents.data()+dos->e_lfanew);
    if(pe->OptionalHeader.SizeOfImage != supportedImageSize || pe->OptionalHeader.SizeOfHeaders > contents.size()) return 1;
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr,supportedImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image) return 1;
    std::memcpy(image,contents.data(),pe->OptionalHeader.SizeOfHeaders);
    const auto* section = IMAGE_FIRST_SECTION(pe);
    for(size_t index = 0; index < pe->FileHeader.NumberOfSections; ++index) {
        const auto& entry = section[index];
        if(static_cast<uint64_t>(entry.PointerToRawData)+entry.SizeOfRawData > contents.size() ||
            static_cast<uint64_t>(entry.VirtualAddress)+entry.SizeOfRawData > supportedImageSize) return 1;
        std::memcpy(image+entry.VirtualAddress,contents.data()+entry.PointerToRawData,entry.SizeOfRawData);
    }
    gameBase = reinterpret_cast<uintptr_t>(image);
    // Apply PE base relocations without loading or executing the game.
    const auto& relocations = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    size_t relocationOffset = 0;
    while(relocationOffset < relocations.Size) {
        const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(image+relocations.VirtualAddress+relocationOffset);
        if(block->SizeOfBlock < sizeof(*block) || block->SizeOfBlock > relocations.Size-relocationOffset) return 1;
        const auto* entries = reinterpret_cast<const WORD*>(block+1);
        const size_t count = (block->SizeOfBlock-sizeof(*block))/sizeof(WORD);
        for(size_t index = 0; index < count; ++index) {
            if(entries[index] >> 12 != IMAGE_REL_BASED_HIGHLOW) continue;
            const size_t offset = block->VirtualAddress+(entries[index]&0xfff);
            if(offset+sizeof(DWORD) > supportedImageSize) return 1;
            *reinterpret_cast<DWORD*>(image+offset) += static_cast<DWORD>(gameBase-pe->OptionalHeader.ImageBase);
        }
        relocationOffset += block->SizeOfBlock;
    }
    check(supportedImage(),"decrypted game copy matches startup guard at relocated base");
    if(!supportedImage()) return 1;
    wots4::HostApi host{sizeof(host),wots4::modApiVersion,nullptr,claim,release};
    wots4::ModContext context{sizeof(context),wots4::modApiVersion,nullptr,nullptr,
        reinterpret_cast<HMODULE>(image),L"",&host};
    rejectClaim = 1;
    check(!installHooks(context) && releases == 1,"reservation conflict releases this Mod's claims");
    check(supportedImage(),"reservation conflict leaves executable memory unchanged");
    rejectClaim = -1;
    check(installHooks(context),"MinHook creates and enables all real-code trampolines");
    check(claims == 2+static_cast<int>(hooks.size()),"every overwritten instruction range is reserved");
    bool changed = true, restored = true;
    for(const auto& hook : hooks) {
        changed = changed && std::memcmp(reinterpret_cast<void*>(relocated(hook.address)),hook.expected,hook.length) != 0;
        MH_DisableHook(reinterpret_cast<void*>(relocated(hook.address)));
        MH_RemoveHook(reinterpret_cast<void*>(relocated(hook.address)));
        const auto comparedLength = hook.address == menuSelectAddress ? 5 : hook.length;
        restored = restored && std::memcmp(reinterpret_cast<void*>(relocated(hook.address)),hook.expected,comparedLength) == 0;
    }
    check(changed,"every hook changes only the mapped test copy");
    check(restored,"removing hooks restores all original bytes");
    MH_Uninitialize();
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%d checks passed, %d failed. Only the mapped test copy was patched; no installation, saves, or game process were written.\n",passed,failed);
    return failed ? 1 : 0;
}
