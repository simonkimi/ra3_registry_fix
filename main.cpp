#include <windows.h>
#include <rpc.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")

namespace {

constexpr wchar_t kRa3Key[] =
    L"Software\\Electronic Arts\\Electronic Arts\\Red Alert 3";
constexpr wchar_t kRa3ErgcKey[] =
    L"Software\\Electronic Arts\\Electronic Arts\\Red Alert 3\\ergc";
constexpr REGSAM kWow32 = KEY_WOW64_32KEY;

struct Args {
  std::wstring action;
  std::wstring path;
  std::wstring key;
  std::wstring result;
};

bool WriteResultFile(const std::wstring& path, bool ok, const std::string& message) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  std::string body = ok ? "OK\n" : "ERR\n";
  body += message;
  if (!message.empty() && message.back() != '\n') {
    body.push_back('\n');
  }

  DWORD written = 0;
  const BOOL success =
      WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
  CloseHandle(file);
  return success && written == body.size();
}

std::string Narrow(const std::wstring& wide) {
  if (wide.empty()) {
    return {};
  }
  const int size =
      WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }
  std::string out(static_cast<size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), size, nullptr, nullptr);
  return out;
}

std::wstring Widen(const std::string& utf8) {
  if (utf8.empty()) {
    return {};
  }
  const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  if (size <= 1) {
    return {};
  }
  std::wstring out(static_cast<size_t>(size - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), size);
  return out;
}

std::wstring FormatWinError(DWORD code) {
  wchar_t* buffer = nullptr;
  const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS;
  const DWORD len = FormatMessageW(flags, nullptr, code, 0,
                                   reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
  std::wstring message = L"Win32 error " + std::to_wstring(code);
  if (len > 0 && buffer != nullptr) {
    message.push_back(L':');
    message.push_back(L' ');
    message.append(buffer, len);
    while (!message.empty() &&
           (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' ')) {
      message.pop_back();
    }
  }
  if (buffer != nullptr) {
    LocalFree(buffer);
  }
  return message;
}

bool ParseArgs(int argc, wchar_t** argv, Args& out, std::string& error) {
  if (argc < 2) {
    error = "missing action (fix)";
    return false;
  }
  out.action = argv[1];
  for (int i = 2; i < argc; ++i) {
    const std::wstring flag = argv[i];
    auto take_value = [&](std::wstring& target) -> bool {
      if (i + 1 >= argc) {
        error = "missing value for " + Narrow(flag);
        return false;
      }
      target = argv[++i];
      return true;
    };
    if (flag == L"--path") {
      if (!take_value(out.path)) {
        return false;
      }
    } else if (flag == L"--key") {
      if (!take_value(out.key)) {
        return false;
      }
    } else if (flag == L"--result") {
      if (!take_value(out.result)) {
        return false;
      }
    } else {
      error = "unknown argument: " + Narrow(flag);
      return false;
    }
  }
  if (out.result.empty()) {
    error = "missing --result";
    return false;
  }
  if (out.action != L"fix") {
    error = "action must be fix";
    return false;
  }
  if (out.path.empty()) {
    error = "fix requires --path";
    return false;
  }
  return true;
}

void ClearGameRegistry() {
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, kRa3Key);
  RegDeleteTreeW(HKEY_CURRENT_USER, kRa3Key);
}

bool SetStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
  const BYTE* data = reinterpret_cast<const BYTE*>(value.c_str());
  const DWORD size =
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
  const LONG status =
      RegSetValueExW(key, name, 0, REG_SZ, data, size);
  return status == ERROR_SUCCESS;
}

bool SetDwordValue(HKEY key, const wchar_t* name, DWORD value) {
  const LONG status = RegSetValueExW(key, name, 0, REG_DWORD,
                                     reinterpret_cast<const BYTE*>(&value), sizeof(value));
  return status == ERROR_SUCCESS;
}

std::wstring NormalizePath(const std::wstring& path) {
  wchar_t full[MAX_PATH] = {};
  const DWORD len = GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
  if (len == 0 || len >= MAX_PATH) {
    return path;
  }
  std::wstring out(full);
  while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) {
    out.pop_back();
  }
  return out;
}

std::wstring GenerateCdKey() {
  UUID uuid{};
  if (UuidCreate(&uuid) != RPC_S_OK) {
    return L"00000000-0000-0000-0000-000000000000";
  }
  wchar_t buffer[64] = {};
  swprintf_s(buffer, L"%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
             static_cast<unsigned long>(uuid.Data1), uuid.Data2, uuid.Data3, uuid.Data4[0],
             uuid.Data4[1], uuid.Data4[2], uuid.Data4[3], uuid.Data4[4], uuid.Data4[5],
             uuid.Data4[6], uuid.Data4[7]);
  return buffer;
}

bool FixGameRegistry(const std::wstring& raw_path, std::wstring cd_key, std::string& error) {
  ClearGameRegistry();

  const std::wstring path = NormalizePath(raw_path);
  if (path.empty()) {
    error = "invalid install path";
    return false;
  }

  if (cd_key.empty()) {
    cd_key = GenerateCdKey();
  }

  const std::wstring readme = path + L"\\Support\\readme.txt";
  const std::wstring cd_drive(1, path[0]);

  HKEY ra3 = nullptr;
  DWORD disposition = 0;
  LONG status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kRa3Key, 0, nullptr, 0,
                                KEY_WRITE | kWow32, nullptr, &ra3, &disposition);
  if (status != ERROR_SUCCESS) {
    error = Narrow(L"create HKLM key failed: " + FormatWinError(static_cast<DWORD>(status)));
    return false;
  }

  auto fail_set = [&](const char* what) {
    error = what;
    RegCloseKey(ra3);
    return false;
  };

  if (!SetStringValue(ra3, L"CD Drive", cd_drive) ||
      !SetStringValue(ra3, L"DisplayName", L"Command & Conquer Red Alert 3") ||
      !SetStringValue(ra3, L"Install Dir", path) ||
      !SetStringValue(ra3, L"Installed From", cd_drive) ||
      !SetStringValue(ra3, L"language", L"English (US)") ||
      !SetStringValue(ra3, L"lastversion", L"") ||
      !SetStringValue(ra3, L"Patch URL", L"http://www.ea.com/redalert") ||
      !SetStringValue(ra3, L"Product GUID", L"{296D8550-CB06-48E4-9A8B-E5034FB64715}") ||
      !SetStringValue(ra3, L"Product Name", L"Command & Conquer Red Alert 3") ||
      !SetStringValue(ra3, L"ProfileFolderName", L"Profiles") ||
      !SetStringValue(ra3, L"Readme", readme) ||
      !SetStringValue(ra3, L"Registration", kRa3ErgcKey) ||
      !SetStringValue(ra3, L"ReplayFolderName", L"Replays") ||
      !SetStringValue(ra3, L"SaveFolderName", L"SaveGames") ||
      !SetStringValue(ra3, L"ScreenshotsFolderName", L"Screenshots") ||
      !SetStringValue(ra3, L"Suppression Exe", L"") ||
      !SetDwordValue(ra3, L"UseLocalUserMaps", 0) ||
      !SetStringValue(ra3, L"UserDataLeafName", L"Red Alert 3")) {
    return fail_set("set HKLM values failed");
  }

  HKEY ergc = nullptr;
  status = RegCreateKeyExW(ra3, L"ergc", 0, nullptr, 0, KEY_WRITE | kWow32, nullptr, &ergc,
                           &disposition);
  if (status != ERROR_SUCCESS) {
    error = Narrow(L"create ergc key failed: " + FormatWinError(static_cast<DWORD>(status)));
    RegCloseKey(ra3);
    return false;
  }
  if (!SetStringValue(ergc, nullptr, cd_key)) {
    RegCloseKey(ergc);
    RegCloseKey(ra3);
    error = "set ergc default value failed";
    return false;
  }
  RegCloseKey(ergc);
  RegCloseKey(ra3);

  HKEY ra3_cu = nullptr;
  status = RegCreateKeyExW(HKEY_CURRENT_USER, kRa3Key, 0, nullptr, 0, KEY_WRITE | kWow32,
                           nullptr, &ra3_cu, &disposition);
  if (status != ERROR_SUCCESS) {
    error = Narrow(L"create HKCU key failed: " + FormatWinError(static_cast<DWORD>(status)));
    return false;
  }
  if (!SetStringValue(ra3_cu, L"Language", L"english")) {
    RegCloseKey(ra3_cu);
    error = "set HKCU Language failed";
    return false;
  }
  RegCloseKey(ra3_cu);
  return true;
}

int Finish(const std::wstring& result_path, bool ok, const std::string& message) {
  if (!result_path.empty()) {
    WriteResultFile(result_path, ok, message);
  }
  return ok ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  Args args;
  std::string parse_error;
  if (!ParseArgs(argc, argv, args, parse_error)) {
    return Finish(args.result, false, parse_error);
  }

  std::string error;
  const bool ok = FixGameRegistry(args.path, args.key, error);
  return Finish(args.result, ok, ok ? "注册表修复成功" : error);
}
