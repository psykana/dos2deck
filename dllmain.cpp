#include <filesystem>
#include <set>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

HMODULE D3D11 = NULL;
FARPROC _D3D11CreateDevice;
extern "C" __declspec(dllexport) void D3D11CreateDevice() { _D3D11CreateDevice(); }

#define LOG_TAG "dos2deck"

[[noreturn]]
void FAIL(const char* msg) {
    MessageBoxA(NULL, msg, LOG_TAG, MB_OK);
    ExitProcess(1);
}

#define NULL_CHECK(val, msg) \
    if (val == NULL) {       \
        FAIL(msg);           \
    }

void ProxyD3D11() {
    WCHAR path[MAX_PATH + 32];
    GetSystemDirectory(path, MAX_PATH);
    wcscat_s(path, L"\\d3d11.dll");
    D3D11 = LoadLibrary(path);
    NULL_CHECK(D3D11, "Failed to load D3D11");
    _D3D11CreateDevice = GetProcAddress(D3D11, "D3D11CreateDevice");
    NULL_CHECK(_D3D11CreateDevice, "Failed to get D3D11CreateDevice");
}

void* DetourFunction64(void* pSource, void* pDestination, int dwLen) {
    BYTE stub[] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,            // jmp qword ptr [$+6]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // dst ptr
    };

    void* pTrampoline = VirtualAlloc(0, dwLen + sizeof(stub), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    NULL_CHECK(pTrampoline, "Failed to allocate trampoline memory");

    DWORD dwOld = 0;
    VirtualProtect(pSource, dwLen, PAGE_EXECUTE_READWRITE, &dwOld);

    // Prepare trampoline
    DWORD_PTR returnAddr = (DWORD_PTR)pSource + dwLen;
    memcpy(stub + 6, &returnAddr, 8);
    memcpy((void*)pTrampoline, pSource, dwLen);
    memcpy((void*)((BYTE*)pTrampoline + dwLen), stub, sizeof(stub));

    // Hook original code
    memcpy(stub + 6, &pDestination, 8);
    memcpy(pSource, stub, sizeof(stub));

    // Write NOPs
    for (int i = sizeof(stub); i < dwLen; i++) {
        *((BYTE*)pSource + i) = 0x90;
    }

    VirtualProtect(pSource, dwLen, dwOld, &dwOld);
    return (void*)pTrampoline;
}

std::string GetGameVersion() {
    HMODULE exe = GetModuleHandle(NULL);
    HRSRC hResource = FindResource(exe, MAKEINTRESOURCE(VS_VERSION_INFO), RT_VERSION);
    NULL_CHECK(hResource, "Error retrieving game version info");
    HGLOBAL hData = LoadResource(exe, hResource);
    NULL_CHECK(hData, "Error retrieving game version info");
    LPVOID pVersion = LockResource(hData);
    NULL_CHECK(pVersion, "Error retrieving game version info");

    UINT verLength;
    VS_FIXEDFILEINFO* fixedFileInfo = NULL;
    VerQueryValue(pVersion, L"\\", (LPVOID*)&fixedFileInfo, &verLength);

    char buf[32];
    int len = std::snprintf(buf, 32, "%hu.%hu.%hu.%hu",
        HIWORD(fixedFileInfo->dwFileVersionMS),
        LOWORD(fixedFileInfo->dwFileVersionMS),
        HIWORD(fixedFileInfo->dwFileVersionLS),
        LOWORD(fixedFileInfo->dwFileVersionLS)
    );

    std::string version(buf, len);
    return version;
}

using OnResize_t = void (__fastcall*)(void*, int, int);
OnResize_t OnResize;

using SetFixedAspectRatio_t = void (__fastcall*)(void*, int, float);
SetFixedAspectRatio_t SetFixedAspectRatio;

struct RenderFrame {
    BYTE unk[24];
    uint32_t h;
    uint32_t w;
};

bool atTargetResolution = false;

void __fastcall SetFixedAspectRatio_hook(RenderFrame* ptr, bool setFixed, float targetAspect) {
    atTargetResolution = ((ptr->h == 1280) && (ptr->w == 800));
    if (atTargetResolution) {
        SetFixedAspectRatio(ptr, false, targetAspect);
    }
    else {
        SetFixedAspectRatio(ptr, setFixed, targetAspect);
    }
}

struct UIObject {
    BYTE unk0[28];
    char* path;
};

// List of 16:9 hardcoded UI elements 
const std::set<std::string> UIObjects {
    "inventorySkillPanel_c",
    "journal_csp",
    "partyInventory_c"
};

void __fastcall OnResize_hook(UIObject* ptr, int h, int w) {
    if (atTargetResolution) {
        std::filesystem::path path(ptr->path);
        std::string currentObject = path.stem().generic_string();
        bool found = UIObjects.find(currentObject) != UIObjects.end();
        if (found) {
            w = 720;
        }
        // edge case: split screen
        if ((h == 640) && (currentObject == "bottomBar_c")) {
            w = 720;
        }
    }

    OnResize(ptr, h, w);
}

struct Offsets {
    DWORD64 SetFixedAspectRatioAddr;
    DWORD64 OnResizeAddr;
};

void InstallPatches(Offsets offsets) {
    HMODULE EoCApp = GetModuleHandle(NULL);

    // Disable 16:9 aspect ratio for Steam Deck resolution
    DWORD64 SetFixedAspectRatioAddr = (DWORD64)EoCApp + offsets.SetFixedAspectRatioAddr;
    SetFixedAspectRatio = (SetFixedAspectRatio_t)DetourFunction64((void*)SetFixedAspectRatioAddr, SetFixedAspectRatio_hook, 18);

    // Revert some UI elements to 16:9
    DWORD64 OnResizeAddr = (DWORD64)EoCApp + offsets.OnResizeAddr;
    OnResize = (OnResize_t)DetourFunction64((void*)OnResizeAddr, OnResize_hook, 17);
}

// Search for GOG Galaxy in the import table
bool IsGOG() {
    HMODULE EoCApp = GetModuleHandle(NULL);
    IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)EoCApp;
    IMAGE_NT_HEADERS* imageNTHeaders = (IMAGE_NT_HEADERS*)((DWORD_PTR)EoCApp + dosHeader->e_lfanew);
    DWORD importDirectoryRVA = imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    IMAGE_SECTION_HEADER* sectionHeader = IMAGE_FIRST_SECTION(imageNTHeaders);
    IMAGE_SECTION_HEADER* importSection = NULL;
    for (int i = 0; i < imageNTHeaders->FileHeader.NumberOfSections; i++) {
        if (importDirectoryRVA >= sectionHeader->VirtualAddress && (importDirectoryRVA < (sectionHeader->VirtualAddress + sectionHeader->SizeOfRawData))) {
            importSection = sectionHeader;
            break;
        }
        sectionHeader++;
    }
    NULL_CHECK(importSection, "GOG/Steam check failed");

    DWORD_PTR RVA = ((DWORD_PTR)(dosHeader)) + importSection->VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR* importDescriptor = (PIMAGE_IMPORT_DESCRIPTOR)(RVA + (imageNTHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress - importSection->VirtualAddress));

    while (importDescriptor->Name != NULL) {
        if (0 == strcmp((LPCSTR)(RVA + importDescriptor->Name - importSection->VirtualAddress), "Galaxy64.dll")) {
            return true;
        }
        importDescriptor++;
    }

    return false;
}

Offsets offsets_steam = { 0x01DAFA60, 0x01245B20 };
Offsets offsets_gog   = { 0x01D61A10, 0x011F7BB0 };

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        if (GetModuleHandle(L"EoCApp.exe") != NULL) {
            ProxyD3D11();
            if (GetGameVersion() != "3.6.117.3735") {
                MessageBoxA(NULL, "Version mismatch, check for updates!", LOG_TAG, MB_OK);
            }
            else {
                if (IsGOG()) {
                    InstallPatches(offsets_gog);
                }
                else {
                    InstallPatches(offsets_steam);
                }
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        if (D3D11 != NULL) {
            FreeLibrary(D3D11);
        }
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
