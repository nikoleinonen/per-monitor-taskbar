#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

class RegKey {
public:
  RegKey() = default;
  ~RegKey() { Close(); }

  RegKey(const RegKey&) = delete;
  RegKey& operator=(const RegKey&) = delete;

  RegKey(RegKey&& other) noexcept : key_(other.key_) { other.key_ = nullptr; }

  RegKey& operator=(RegKey&& other) noexcept {
    if (this != &other) {
      Close();
      key_ = other.key_;
      other.key_ = nullptr;
    }
    return *this;
  }

  bool Open(HKEY parent, const wchar_t* subkey, REGSAM access = KEY_READ) {
    Close();
    return RegOpenKeyExW(parent, subkey, 0, access, &key_) == ERROR_SUCCESS;
  }

  bool Create(HKEY parent, const wchar_t* subkey,
              REGSAM access = KEY_ALL_ACCESS) {
    Close();
    return RegCreateKeyExW(parent, subkey, 0, nullptr, 0, access, nullptr,
                           &key_, nullptr) == ERROR_SUCCESS;
  }

  void Close() {
    if (key_) {
      RegCloseKey(key_);
      key_ = nullptr;
    }
  }

  HKEY Get() const { return key_; }
  explicit operator bool() const { return key_ != nullptr; }

  std::optional<DWORD> ReadDword(const wchar_t* name) const {
    DWORD type = 0, data = 0, size = sizeof(data);
    if (RegQueryValueExW(key_, name, nullptr, &type,
                         reinterpret_cast<BYTE*>(&data),
                         &size) != ERROR_SUCCESS ||
        type != REG_DWORD)
      return std::nullopt;
    return data;
  }

  std::vector<BYTE> ReadBinary(const wchar_t* name) const {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key_, name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        type != REG_BINARY || size == 0)
      return {};
    std::vector<BYTE> data(size);
    if (RegQueryValueExW(key_, name, nullptr, nullptr, data.data(), &size) !=
        ERROR_SUCCESS)
      return {};
    return data;
  }

  std::wstring ReadString(const wchar_t* name) const {
    DWORD type = 0, size = 0;
    if (RegQueryValueExW(key_, name, nullptr, &type, nullptr, &size) !=
            ERROR_SUCCESS ||
        type != REG_SZ || size < sizeof(wchar_t))
      return {};
    std::wstring data(size / sizeof(wchar_t), L'\0');
    if (RegQueryValueExW(key_, name, nullptr, nullptr,
                         reinterpret_cast<BYTE*>(data.data()),
                         &size) != ERROR_SUCCESS)
      return {};
    while (!data.empty() && data.back() == L'\0')
      data.pop_back();
    return data;
  }

  bool WriteDword(const wchar_t* name, DWORD value) {
    return RegSetValueExW(key_, name, 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value),
                          sizeof(value)) == ERROR_SUCCESS;
  }

  bool WriteBinary(const wchar_t* name, const BYTE* data, DWORD size) {
    return RegSetValueExW(key_, name, 0, REG_BINARY, data, size) ==
           ERROR_SUCCESS;
  }

  bool WriteString(const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(
               key_, name, 0, REG_SZ,
               reinterpret_cast<const BYTE*>(value.c_str()),
               static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
  }

  bool DeleteValue(const wchar_t* name) {
    return RegDeleteValueW(key_, name) == ERROR_SUCCESS;
  }

private:
  HKEY key_ = nullptr;
};
