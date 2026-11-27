#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <optional>
#include <span>

// ============================================================
//  External process reader — read-only, never writes game memory.
//  Uses ReadProcessMemory exclusively.
// ============================================================

class ProcessReader {
public:
    ProcessReader() = default;
    ~ProcessReader() { close(); }

    ProcessReader(const ProcessReader&) = delete;
    ProcessReader& operator=(const ProcessReader&) = delete;

    // Finds RocketLeague.exe, opens it with PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION.
    // Returns false if the process is not running or access is denied.
    bool open(const char* process_name);

    // Returns true while the handle is valid and the process is still alive.
    bool is_alive() const;

    // Closes the handle. Safe to call multiple times.
    void close();

    // Raw read — returns false if any byte of [addr, addr+size) is unreadable.
    bool read_raw(uintptr_t addr, void* out, size_t size) const;

    // Type-safe read helper.
    template<typename T>
    bool read(uintptr_t addr, T& out) const {
        return read_raw(addr, &out, sizeof(T));
    }

    template<typename T>
    std::optional<T> read_opt(uintptr_t addr) const {
        T val{};
        if (!read_raw(addr, &val, sizeof(T))) return std::nullopt;
        return val;
    }

    // Follow a pointer chain starting from base.
    // offsets: each offset is added to the value read at the previous step.
    // Returns nullopt on any failed read or null pointer.
    std::optional<uintptr_t> resolve_chain(uintptr_t base,
                                           std::span<const uint32_t> offsets) const;

    // Read a null-terminated ASCII string (up to max_len bytes).
    std::string read_string(uintptr_t addr, size_t max_len = 256) const;

    // Read a null-terminated UTF-16LE string.
    std::wstring read_wstring(uintptr_t addr, size_t max_chars = 256) const;

    // Scan [region_start, region_start+region_size) for the given byte pattern.
    // pattern: bytes, 0xCC = wildcard.
    // Returns the VA of the first match, or 0 if not found.
    uintptr_t aob_scan(uintptr_t region_start, size_t region_size,
                       const uint8_t* pattern, const uint8_t* mask, size_t pat_size) const;

    // Convenience: scan entire readable pages of the main module.
    uintptr_t aob_scan_module(const uint8_t* pattern, const uint8_t* mask, size_t pat_size) const;

    uintptr_t module_base()  const { return module_base_; }
    size_t    module_size()  const { return module_size_; }
    DWORD     pid()          const { return pid_; }

private:
    HANDLE    handle_      = INVALID_HANDLE_VALUE;
    DWORD     pid_         = 0;
    uintptr_t module_base_ = 0;
    size_t    module_size_ = 0;

    bool find_module(const char* exe_name);
};
