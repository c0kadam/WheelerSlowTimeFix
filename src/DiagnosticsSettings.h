#pragma once

#include <Windows.h>
#include <array>
#include <string_view>

namespace OwnershipDiagnostics::Settings
{
	enum class Status { Enabled, Disabled, MissingFile, MissingSetting, Invalid, Unreadable, PathUnavailable };

	inline Status Parse(std::wstring_view value) noexcept
	{
		const auto first = value.find_first_not_of(L" \t\r\n");
		if (first == std::wstring_view::npos) { return Status::Invalid; }
		value = value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
		const auto equals = [&](std::wstring_view expected) {
			if (value.size() != expected.size()) { return false; }
			for (std::size_t i = 0; i < value.size(); ++i) {
				const auto ch = value[i] >= L'A' && value[i] <= L'Z' ? value[i] + (L'a' - L'A') : value[i];
				if (ch != expected[i]) { return false; }
			}
			return true;
		};
		if (equals(L"true")) { return Status::Enabled; }
		if (equals(L"false")) { return Status::Disabled; }
		return Status::Invalid;
	}

	inline Status Read(const wchar_t* path) noexcept
	{
		if (!path || !*path) { return Status::PathUnavailable; }
		const HANDLE file = ::CreateFileW(path, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			const auto error = ::GetLastError();
			return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? Status::MissingFile : Status::Unreadable;
		}
		::CloseHandle(file);
		std::array<wchar_t, 64> value{};
		const auto length = ::GetPrivateProfileStringW(L"Diagnostics", L"Enabled", L"", value.data(),
			static_cast<DWORD>(value.size()), path);
		if (length == 0) { return Status::MissingSetting; }
		if (length >= value.size() - 1) { return Status::Invalid; }
		return Parse(std::wstring_view(value.data(), length));
	}

	inline const char* Description(Status status) noexcept
	{
		switch (status) {
		case Status::Enabled: return "Enabled=true";
		case Status::Disabled: return "Enabled=false";
		case Status::MissingFile: return "INI missing";
		case Status::MissingSetting: return "setting missing or empty";
		case Status::Invalid: return "invalid setting";
		case Status::Unreadable: return "INI unreadable";
		default: return "INI path unavailable";
		}
	}
}
