#include "process_reader.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <vector>
#include <cstring>
#include <cassert>

// ── open ─────────────────────────────────────────────────────────────────────
bool ProcessReader::open(const char* process_name) {
    close();

    // Find PID via snapshot
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{ .dwSize = sizeof(pe) };
    bool found = false;

    // Convert process_name to wide for comparison
    wchar_t wname[MAX_PATH]{};
    MultiByteToWideChar(CP_UTF8, 0, process_name, -1, wname, MAX_PATH);

    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wname) == 0) {
                pid_  = pe.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    if (!found) return false;

    handle_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION,
                          FALSE, pid_);
    if (handle_ == INVALID_HANDLE_VALUE || handle_ == nullptr) {
        pid_ = 0;
        return false;
    }

    return find_module(process_name);
}

// ── find_module ───────────────────────────────────────────────────────────────
bool ProcessReader::find_module(const char* exe_name) {
    HMODULE mods[1024]{};
    DWORD   needed = 0;

    if (!EnumProcessModulesEx(handle_, mods, sizeof(mods), &needed,
                              LIST_MODULES_64BIT)) {
        return false;
    }

    wchar_t wname[MAX_PATH]{};
    MultiByteToWideChar(CP_UTF8, 0, exe_name, -1, wname, MAX_PATH);

    const DWORD count = needed / sizeof(HMODULE);
    for (DWORD i = 0; i < count; ++i) {
        wchar_t mod_name[MAX_PATH]{};
        if (!GetModuleBaseNameW(handle_, mods[i], mod_name, MAX_PATH)) continue;

        if (_wcsicmp(mod_name, wname) == 0) {
            MODULEINFO mi{};
            if (GetModuleInformation(handle_, mods[i], &mi, sizeof(mi))) {
                module_base_ = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
                module_size_ = mi.SizeOfImage;
                return true;
            }
        }
    }
    return false;
}

// ── is_alive ──────────────────────────────────────────────────────────────────
bool ProcessReader::is_alive() const {
    if (!handle_ || handle_ == INVALID_HANDLE_VALUE) return false;
    DWORD exit_code = 0;
    return GetExitCodeProcess(handle_, &exit_code) && exit_code == STILL_ACTIVE;
}

// ── close ─────────────────────────────────────────────────────────────────────
void ProcessReader::close() {
    if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    pid_         = 0;
    module_base_ = 0;
    module_size_ = 0;
}

// ── read_raw ──────────────────────────────────────────────────────────────────
bool ProcessReader::read_raw(uintptr_t addr, void* out, size_t size) const {
    if (!handle_ || addr == 0 || out == nullptr || size == 0) return false;
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr),
                           out, size, &bytes_read)) {
        return false;
    }
    return bytes_read == size;
}

// ── resolve_chain ─────────────────────────────────────────────────────────────
std::optional<uintptr_t> ProcessReader::resolve_chain(
    uintptr_t base, std::span<const uint32_t> offsets) const
{
    uintptr_t cur = base;
    for (size_t i = 0; i < offsets.size(); ++i) {
        cur += offsets[i];
        if (i + 1 < offsets.size()) {
            uintptr_t next = 0;
            if (!read(cur, next) || next == 0) return std::nullopt;
            cur = next;
        }
    }
    return cur;
}

// ── read_string ───────────────────────────────────────────────────────────────
std::string ProcessReader::read_string(uintptr_t addr, size_t max_len) const {
    std::string out(max_len, '\0');
    size_t bytes_read = 0;
    ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr),
                      out.data(), max_len, reinterpret_cast<SIZE_T*>(&bytes_read));
    out.resize(strnlen(out.c_str(), bytes_read));
    return out;
}

// ── read_wstring ──────────────────────────────────────────────────────────────
std::wstring ProcessReader::read_wstring(uintptr_t addr, size_t max_chars) const {
    std::wstring out(max_chars, L'\0');
    size_t bytes_read = 0;
    ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr),
                      out.data(), max_chars * sizeof(wchar_t),
                      reinterpret_cast<SIZE_T*>(&bytes_read));
    out.resize(wcsnlen(out.c_str(), bytes_read / sizeof(wchar_t)));
    return out;
}

// ── aob_scan ─────────────────────────────────────────────────────────────────
uintptr_t ProcessReader::aob_scan(
    uintptr_t region_start, size_t region_size,
    const uint8_t* pattern, const uint8_t* mask, size_t pat_size) const
{
    constexpr size_t CHUNK = 0x10000;
    std::vector<uint8_t> buf(CHUNK);

    uintptr_t end = region_start + region_size;
    for (uintptr_t addr = region_start; addr < end; addr += CHUNK) {
        size_t to_read = std::min(CHUNK, static_cast<size_t>(end - addr));
        SIZE_T nr = 0;
        if (!ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(addr),
                               buf.data(), to_read, &nr) || nr == 0) continue;

        for (size_t i = 0; i + pat_size <= nr; ++i) {
            bool match = true;
            for (size_t j = 0; j < pat_size; ++j) {
                if (mask[j] == 'x' && buf[i+j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) return addr + i;
        }
    }
    return 0;
}

// ── aob_scan_module ───────────────────────────────────────────────────────────
uintptr_t ProcessReader::aob_scan_module(
    const uint8_t* pattern, const uint8_t* mask, size_t pat_size) const
{
    if (!module_base_ || !module_size_) return 0;
    return aob_scan(module_base_, module_size_, pattern, mask, pat_size);
}
