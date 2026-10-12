#include "../src/mod.cpp"
#include <cstdio>
namespace {
int passed, failed;
void check(bool condition, const char* name) {
    std::printf("%s: %s\n",condition ? "PASS" : "FAIL",name);
    condition ? ++passed : ++failed;
}
const wchar_t* appendedLabel;
uint32_t appendedFlags;
uint32_t appendedColumn;
BYTE* appendedMenu;
void __stdcall mockClearRows(void* pointer) {
    auto* menu = static_cast<BYTE*>(pointer);
    *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset) = *reinterpret_cast<uint32_t**>(menu+menuFlagsBeginOffset);
}
__declspec(naked) void mockAppendRow() {
    __asm {
        mov appendedLabel, ecx
        mov appendedColumn, edx
        mov appendedMenu, edi
        mov eax, [esp+4]
        mov appendedFlags, eax
        add dword ptr [edi+menuFlagsEndOffset], 4
        ret 4
    }
}
__declspec(naked) void mockSelectionContinuation() {
    __asm {
        mov byte ptr [eax+18h], dl
        ret
    }
}
__declspec(naked) void runSelectionHook(BYTE* screen, int row) {
    __asm {
        push ebx
        mov ebx, [esp+8]
        mov eax, [esp+12]
        call menuSelectHook
        pop ebx
        ret
    }
}
__declspec(naked) void returnSaveLabel() {
    __asm {
        mov eax, ecx
        ret
    }
}
__declspec(naked) const wchar_t* runSaveLabelHook(int difficulty) {
    __asm {
        mov ecx, [esp+4]
        call saveLabelHook
        ret
    }
}
}
int main() {
    auto* image = static_cast<BYTE*>(VirtualAlloc(nullptr,supportedImageSize,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!image) return 1;
    gameBase = reinterpret_cast<uintptr_t>(image);
    appendRowFunction = mockAppendRow;
    clearRowsFunction = mockClearRows;
    BYTE screen[0x80] = {}, menu[0x100] = {};
    uint32_t flags[5] = {1,1,1,1,1};
    BYTE gameState[0x20] = {};
    BYTE* gameStateInstance = gameState;
    gameStatePointer = reinterpret_cast<uintptr_t>(&gameStateInstance);
    selectionContinuation = reinterpret_cast<uintptr_t>(mockSelectionContinuation);
    auto* mapping = reinterpret_cast<uint16_t*>(screen+difficultyMapOffset);
    check(std::wcscmp(modeLabel(L"易しい"),L"一撃死") == 0,"Japanese difficulty label");
    check(std::wcscmp(modeLabel(L"Easy"),L"Harakiri") == 0,"English difficulty label");
    check(std::wcscmp(modeLabel(nullptr),L"Harakiri") == 0,"missing translation uses Harakiri");
    check(std::wcscmp(nativeDifficultyLabel(3),L"VeryHard") == 0,"native English VeryHard label differs from Harakiri");
    check(std::wcscmp(saveDifficultyLabel(4),L"Harakiri") == 0,"Harakiri save slot has a separate label");
    saveLabelContinuation = reinterpret_cast<uintptr_t>(returnSaveLabel);
    originalSaveLabel = returnSaveLabel;
    check(std::wcscmp(runSaveLabelHook(4),L"Harakiri") == 0,"save-label bridge avoids indexing the native four-entry table");
    check(std::wcscmp(runSaveLabelHook(3),L"VeryHard") == 0,"native VeryHard save slot label differs from Harakiri");
    check(reinterpret_cast<uintptr_t>(runSaveLabelHook(1)) == 1,"native Normal label follows its original continuation");
    for(int count = 2; count <= 4; ++count) {
        for(int index = 0; index < 4; ++index) mapping[index] = index < count ? index : 0xffff;
        *reinterpret_cast<uint32_t**>(menu+menuFlagsBeginOffset) = flags;
        *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset) = flags+count;
        check(appendModeRow(screen,menu,L"Harakiri") && harakiriRow == count,"append after unlocked native difficulties");
        check(appendedMenu == menu && appendedColumn == 1 && appendedFlags == 1 &&
            std::wcscmp(appendedLabel,L"Harakiri") == 0,"native append calling convention and selectable flag");
        check(selectDifficulty(screen,count) == 3 && selectedDifficulty == Difficulty::Harakiri,"Harakiri uses Very Hard behavior and a separate enum");
        runSelectionHook(screen,count);
        check(gameState[0x18] == 3 && selectedDifficulty == Difficulty::Harakiri,"selection bridge delivers Very Hard to the native store");
        runSelectionHook(screen,1);
        check(gameState[0x18] == 1 && selectedDifficulty == Difficulty::Normal,"Normal selection bridge disables lethal attacks");
        for(int index = 0; index < count; ++index)
            check(selectDifficulty(screen,index) == index && selectedDifficulty != Difficulty::Harakiri,"native choice disables Harakiri and keeps its mapping");
        check(selectDifficulty(screen,-1) == 1 && selectedDifficulty == Difficulty::Normal,"invalid choice cannot read before native mapping");
        check(selectDifficulty(screen,5) == 1 && selectedDifficulty == Difficulty::Normal,"invalid choice cannot read after native mapping");
    }
    *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset) = flags+5;
    check(!appendModeRow(screen,menu,L"Harakiri") && harakiriRow == -1,"unexpected existing fifth row is refused");
    mapping[3] = 0xffff;
    check(selectDifficulty(screen,3) == 1 && selectedDifficulty == Difficulty::Normal,"locked native mapping is not stored as a difficulty");
    mapping[0] = 0; mapping[1] = 1; mapping[2] = 3;
    *reinterpret_cast<uint32_t**>(menu+menuFlagsEndOffset) = flags+3;
    check(appendModeRow(screen,menu,L"Harakiri") && selectDifficulty(screen,2) == 3 &&
        selectedDifficulty == Difficulty::VeryHard,"noncontiguous unlock mapping preserves native Very Hard");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("%d checks passed, %d failed. No game installation, saves, or game process were written.\n",passed,failed);
    return failed ? 1 : 0;
}
