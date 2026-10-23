#pragma once

#include <Windows.h>
#include <TlHelp32.h>
#include <processsnapshot.h>
#include <type_traits>
#include <utility>
#include <vector>

namespace ThreadDiscovery
{
	class ThreadPin
	{
	public:
		ThreadPin() = default;
		explicit ThreadPin(HANDLE a_handle) noexcept : handle_(a_handle) {}
		~ThreadPin() { reset(); }
		ThreadPin(const ThreadPin&) = delete;
		ThreadPin& operator=(const ThreadPin&) = delete;
		ThreadPin(ThreadPin&& a_other) noexcept : handle_(std::exchange(a_other.handle_, nullptr)) {}
		ThreadPin& operator=(ThreadPin&& a_other) noexcept
		{
			if (this != &a_other) { reset(std::exchange(a_other.handle_, nullptr)); }
			return *this;
		}
		void reset(HANDLE a_handle = nullptr) noexcept
		{
			if (handle_ && handle_ != INVALID_HANDLE_VALUE) { ::CloseHandle(handle_); }
			handle_ = a_handle;
		}
		HANDLE get() const noexcept { return handle_; }
	private:
		HANDLE handle_{ nullptr };
	};

	struct PssApi
	{
		// SDK function declarations are hidden by CommonLib's Windows 7 target.
		// Use documented signatures without increasing that target or importing PSS.
		using CaptureFn = DWORD (WINAPI*)(HANDLE, PSS_CAPTURE_FLAGS, DWORD, HPSS*);
		using FreeFn = DWORD (WINAPI*)(HANDLE, HPSS);
		using CreateMarkerFn = DWORD (WINAPI*)(const PSS_ALLOCATOR*, HPSSWALK*);
		using FreeMarkerFn = DWORD (WINAPI*)(HPSSWALK);
		using WalkFn = DWORD (WINAPI*)(HPSS, PSS_WALK_INFORMATION_CLASS, HPSSWALK, void*, DWORD);
		CaptureFn capture{};
		FreeFn freeSnapshot{};
		CreateMarkerFn createMarker{};
		FreeMarkerFn freeMarker{};
		WalkFn walk{};
		bool available() const noexcept { return capture && freeSnapshot && createMarker && freeMarker && walk; }
	};
#if NTDDI_VERSION >= NTDDI_WINBLUE
	static_assert(std::is_same_v<PssApi::CaptureFn, decltype(&::PssCaptureSnapshot)>);
	static_assert(std::is_same_v<PssApi::FreeFn, decltype(&::PssFreeSnapshot)>);
	static_assert(std::is_same_v<PssApi::CreateMarkerFn, decltype(&::PssWalkMarkerCreate)>);
	static_assert(std::is_same_v<PssApi::FreeMarkerFn, decltype(&::PssWalkMarkerFree)>);
	static_assert(std::is_same_v<PssApi::WalkFn, decltype(&::PssWalkSnapshot)>);
#endif

	inline const PssApi& GetPssApi()
	{
		// Optional resolution preserves the Toolhelp path on pre-Windows 8.1 systems.
		static const PssApi api = [] {
			PssApi result;
			if (const auto module = ::GetModuleHandleW(L"kernel32.dll")) {
				result.capture = reinterpret_cast<decltype(result.capture)>(::GetProcAddress(module, "PssCaptureSnapshot"));
				result.freeSnapshot = reinterpret_cast<decltype(result.freeSnapshot)>(::GetProcAddress(module, "PssFreeSnapshot"));
				result.createMarker = reinterpret_cast<decltype(result.createMarker)>(::GetProcAddress(module, "PssWalkMarkerCreate"));
				result.freeMarker = reinterpret_cast<decltype(result.freeMarker)>(::GetProcAddress(module, "PssWalkMarkerFree"));
				result.walk = reinterpret_cast<decltype(result.walk)>(::GetProcAddress(module, "PssWalkSnapshot"));
			}
			return result;
		}();
		return api;
	}

	struct Snapshot
	{
		bool complete{ false };
		bool usedPss{ false };
		DWORD error{ ERROR_SUCCESS };
		std::vector<DWORD> ids;
	};

	inline Snapshot CaptureToolhelp()
	{
		Snapshot result;
		const auto raw = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (raw == INVALID_HANDLE_VALUE) { result.error = ::GetLastError(); return result; }
		ThreadPin snapshot(raw);
		const auto processID = ::GetCurrentProcessId();
		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		if (::Thread32First(raw, &entry)) {
			do {
				if (entry.th32OwnerProcessID == processID) { result.ids.push_back(entry.th32ThreadID); }
			} while (::Thread32Next(raw, &entry));
		}
		result.error = ::GetLastError();
		result.complete = result.error == ERROR_NO_MORE_FILES && !result.ids.empty();
		return result;
	}

	inline Snapshot CapturePss(const PssApi& a_api)
	{
		Snapshot result;
		result.usedPss = true;
		if (!a_api.available()) { result.error = ERROR_NOT_SUPPORTED; return result; }
		const auto process = ::GetCurrentProcess();
		HPSS snapshot{};
		// IDs only: no VA clone, thread contexts, handles, suspension or debug-register requests.
		result.error = a_api.capture(process, PSS_CAPTURE_THREADS, 0, &snapshot);
		if (result.error != ERROR_SUCCESS) { return result; }
		struct SnapshotOwner
		{
			const PssApi& api; HANDLE process; HPSS snapshot;
			~SnapshotOwner() { api.freeSnapshot(process, snapshot); }
		} owner{ a_api, process, snapshot };
		HPSSWALK marker{};
		result.error = a_api.createMarker(nullptr, &marker);
		if (result.error != ERROR_SUCCESS) { return result; }
		struct MarkerOwner
		{
			const PssApi& api; HPSSWALK marker;
			~MarkerOwner() { api.freeMarker(marker); }
		} markerOwner{ a_api, marker };
		PSS_THREAD_ENTRY entry{};
		const auto processID = ::GetCurrentProcessId();
		while ((result.error = a_api.walk(snapshot, PSS_WALK_THREADS, marker, &entry, sizeof(entry))) == ERROR_SUCCESS) {
			if (entry.ProcessId != processID || entry.ThreadId == 0) {
				result.error = ERROR_INVALID_DATA;
				return result;
			}
			// PSS can report terminated thread objects kept alive by our pins.
			if ((entry.Flags & PSS_THREAD_FLAGS_TERMINATED) != 0) { continue; }
			result.ids.push_back(entry.ThreadId);
		}
		result.complete = result.error == ERROR_NO_MORE_ITEMS && !result.ids.empty();
		return result;
	}

	inline Snapshot Capture(const PssApi& a_api = GetPssApi())
	{
		auto result = CapturePss(a_api);
		if (result.complete) { return result; }
		// Never publish a partial list or prune registrations based on a failed capture.
		return CaptureToolhelp();
	}
}
