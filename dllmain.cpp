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

    const DWORD minLen = 14;
    BYTE stub[minLen] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,            // jmp qword ptr [$+6]
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 // dst ptr
    };

    void* pTrampoline = VirtualAlloc(0, dwLen + sizeof(stub), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    NULL_CHECK(pTrampoline, "Failed to allocate trampoline memory");

    DWORD dwOld = 0;
    VirtualProtect(pSource, dwLen, PAGE_EXECUTE_READWRITE, &dwOld);

    DWORD64 retto = (DWORD64)pSource + dwLen;

    // trampoline
    memcpy(stub + 6, &retto, 8);
    memcpy((void*)((DWORD_PTR)pTrampoline), pSource, dwLen);
    memcpy((void*)((DWORD_PTR)pTrampoline + dwLen), stub, sizeof(stub));

    // orig
    memcpy(stub + 6, &pDestination, 8);
    memcpy(pSource, stub, sizeof(stub));

    for (int i = 14; i < dwLen; i++) {
        *(BYTE*)((DWORD_PTR)pSource + i) = 0x90;
    }

    VirtualProtect(pSource, dwLen, dwOld, &dwOld);
    return (void*)((DWORD_PTR)pTrampoline);
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

using OnResize_t = uintptr_t(__fastcall*)(void*, int, int);
OnResize_t OnResize;

using SetFixedAspectRatio_t = void(__fastcall*)(void*, int, float);
SetFixedAspectRatio_t SetFixedAspectRatio;

typedef struct _RenderFrame_s {
    BYTE unk[24];
    uint32_t h;
    uint32_t w;
} RenderFrame;

void __fastcall SetFixedAspectRatio_hook(RenderFrame* ptr, bool setFixed, float targetAspect) {
    if ((ptr->h == 1280) && (ptr->w == 800)) {
        setFixed = false;
    }
    SetFixedAspectRatio(ptr, setFixed, targetAspect);
}

struct UIObject {
    BYTE unk0[28];
    char* path;
    BYTE unk1[56];
    DWORD layout;
};

// List of 16:9 hardcoded UI elements 
const std::set<std::string> UIObjects {
    "inventorySkillPanel_c",
    "journal_csp",
    "partyInventory_c"
};

void __fastcall OnResize_hook(UIObject* ptr, int h, int w) {

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

    OnResize(ptr, h, w);
}

void InstallPatches() {
    HMODULE EoCApp = GetModuleHandle(NULL);

    // Disable 16:9 aspect ratio for Steam Deck resolution
    DWORD64 SetFixedAspectRatioAddr = (DWORD64)EoCApp + 0x1DAFA60;
    SetFixedAspectRatio = (SetFixedAspectRatio_t)DetourFunction64((void*)SetFixedAspectRatioAddr, SetFixedAspectRatio_hook, 18);

    // Revert some UI elements to 16:9
    DWORD64 OnResizeAddr = (DWORD64)EoCApp + 0x1245B20;
    OnResize = (OnResize_t)DetourFunction64((void*)OnResizeAddr, OnResize_hook, 17);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        if (GetModuleHandle(L"EoCApp.exe") != NULL) {
            ProxyD3D11();
            if (GetGameVersion() != "3.6.117.3735") {
                MessageBoxA(NULL, "Version mismatch, check for updates!", LOG_TAG, MB_OK);
            }
            else {
                InstallPatches();
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
