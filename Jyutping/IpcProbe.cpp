#include "IpcProbe.h"

#ifdef JYUTPING_IPC_PROBE

#include "Globals.h"
#include "Logger.h"

#ifdef JYUTPING_BACKEND_RPC
#include "InstallPath.h"
#include "Settings.h"
#include <array>
#endif

#ifdef JYUTPING_RPC_PROBE
#ifndef JYUTPING_BACKEND_RPC
#include "../experiments/ipc-probe/appcontainer/RpcProbeClient.h"
#endif
#endif

#include <Aclapi.h>
#include <CommCtrl.h>
#include <Sddl.h>

#include <mutex>
#include <new>
#include <optional>
#include <string>
#include <string_view>

#pragma comment(lib, "comctl32.lib")

namespace {

constexpr UINT ResultMessage = WM_APP + 1;
#ifdef JYUTPING_RPC_PROBE
constexpr auto SelectionTimeout = std::chrono::milliseconds(300);
#else
constexpr DWORD ReplyTimeoutMs = 250;
#endif
#ifdef JYUTPING_BACKEND_RPC
constexpr UINT_PTR HealthTimerId = 1;
constexpr UINT_PTR SettingsTimerId = 2;
constexpr unsigned int InitialHealthDelayMs = 500;
constexpr unsigned int MaxHealthDelayMs = 8000;
constexpr unsigned int SettingsPollMs = 1000;
#endif

void WriteProbeLog(const wchar_t* line)
{
#ifdef JYUTPING_IPC_PROBE_SMOKE
    OutputDebugStringW(line);
#else
    Global::Log(L"%ls", line);
#endif
}

#ifdef JYUTPING_BACKEND_RPC
DWORD LaunchInstalledBackend()
{
    if (!SettingsStore().CanPersist())
    {
        return ERROR_ACCESS_DENIED;
    }
    auto token = GetCurrentProcessToken();
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    if (!GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes))
    {
        return GetLastError();
    }
    alignas(TOKEN_MANDATORY_LABEL) std::array<BYTE, sizeof(TOKEN_MANDATORY_LABEL) + SECURITY_MAX_SID_SIZE> integrity{};
    if (!GetTokenInformation(token, TokenIntegrityLevel, integrity.data(), static_cast<DWORD>(integrity.size()), &bytes))
    {
        return GetLastError();
    }
    auto sid = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;
    const DWORD level = *GetSidSubAuthority(sid, *GetSidSubAuthorityCount(sid) - 1);
    if (elevation.TokenIsElevated || level < SECURITY_MANDATORY_MEDIUM_RID || level >= SECURITY_MANDATORY_HIGH_RID)
    {
        return ERROR_ACCESS_DENIED;
    }

    const auto directory = InstalledJyutpingDirectory();
    if (directory.empty())
    {
        return ERROR_PATH_NOT_FOUND;
    }
    const auto executable = directory + L"\\JyutpingServer.exe";
    std::wstring command = L"\"" + executable + L"\" --installed";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, directory.c_str(), &startup, &process))
    {
        return GetLastError();
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ERROR_SUCCESS;
}
#endif

struct Reply
{
    const wchar_t* stage = L"connect";
    DWORD error = ERROR_SUCCESS;
    bool candidate = false;
    LONGLONG connectMicroseconds = 0;
#ifdef JYUTPING_RPC_PROBE
    BackendEngineReply suggestions;
#endif
#ifdef JYUTPING_BACKEND_RPC
    std::optional<BackendSettingsSnapshot> settings;
    uint64_t settingsEpoch = 0;
#endif
};

LONGLONG Microseconds(IpcProbe::Clock::duration duration)
{
    return std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
}

#ifndef JYUTPING_BACKEND_RPC
struct ProbeIdentity
{
    DWORD sessionId = 0;
    alignas(TOKEN_USER) BYTE tokenData[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    std::wstring suffix;

    PSID UserSid() const { return reinterpret_cast<const TOKEN_USER*>(tokenData)->User.Sid; }
};

Reply ReadProbeIdentity(ProbeIdentity& identity)
{
    HANDLE tokenHandle = nullptr;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &identity.sessionId) || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tokenHandle))
    {
        return {L"process identity", GetLastError()};
    }
    std::unique_ptr<void, decltype(&CloseHandle)> token(tokenHandle, CloseHandle);
    DWORD tokenDataSize = 0;
    if (!GetTokenInformation(token.get(), TokenUser, identity.tokenData, sizeof(identity.tokenData), &tokenDataSize))
    {
        return {L"user identity", GetLastError()};
    }
    wchar_t* sidText = nullptr;
    if (!ConvertSidToStringSidW(identity.UserSid(), &sidText))
    {
        return {L"format identity", GetLastError()};
    }
    std::unique_ptr<wchar_t, decltype(&LocalFree)> sidString(sidText, LocalFree);
    identity.suffix = std::to_wstring(identity.sessionId) + L"." + sidString.get();
    return {};
}

#ifdef JYUTPING_RPC_PROBE
Reply RequestCandidate(std::wstring_view spell)
#else
Reply RequestCandidate()
#endif
{
#ifndef JYUTPING_RPC_PROBE
    auto connectStart = IpcProbe::Clock::now();
#endif
    ProbeIdentity identity;
    Reply identityResult = ReadProbeIdentity(identity);
    if (identityResult.error != ERROR_SUCCESS)
    {
        return identityResult;
    }
#ifdef JYUTPING_RPC_PROBE
    const std::wstring endpoint = L"Jyutping.RpcProbe.v2." + identity.suffix;
    if (spell.empty())
    {
        const DWORD status = CallRpc(endpoint.c_str());
        return {L"local RPC", status, status == ERROR_SUCCESS};
    }
    Reply reply{L"local RPC"};
    RpcProbeCandidate candidate;
    reply.error = QueryRpcProbe(endpoint.c_str(), spell, candidate);
    reply.candidate = !candidate.text.empty();
    if (reply.candidate)
    {
        reply.suggestions.candidates.push_back({0, static_cast<uint32_t>(spell.size()), candidate.text, candidate.romanization,
            std::wstring(spell), std::move(candidate.text), std::move(candidate.romanization)});
    }
    return reply;
#else
    std::wstring pipeName = L"\\\\.\\pipe\\Jyutping.IpcProbe.v0." + identity.suffix;
    DWORD access = FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE;
    HANDLE pipeHandle = CreateFileW(pipeName.c_str(), access, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (pipeHandle == INVALID_HANDLE_VALUE)
    {
        return {L"connect", GetLastError()};
    }
    std::unique_ptr<void, decltype(&CloseHandle)> pipe(pipeHandle, CloseHandle);
    LONGLONG connectMicroseconds = Microseconds(IpcProbe::Clock::now() - connectStart);

    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    DWORD status = GetSecurityInfo(pipe.get(), SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &securityDescriptor);
    std::unique_ptr<void, decltype(&LocalFree)> descriptor(securityDescriptor, LocalFree);
    if (status != ERROR_SUCCESS)
    {
        return {L"pipe owner", status};
    }
    ULONG serverSessionId = 0;
    if (!GetNamedPipeServerSessionId(pipe.get(), &serverSessionId))
    {
        return {L"pipe session", GetLastError()};
    }
    if (!owner || !EqualSid(owner, identity.UserSid()) || serverSessionId != identity.sessionId)
    {
        return {L"pipe identity", ERROR_ACCESS_DENIED};
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe.get(), &mode, nullptr, nullptr))
    {
        return {L"pipe mode", GetLastError()};
    }

    std::unique_ptr<void, decltype(&CloseHandle)> event(CreateEventW(nullptr, TRUE, FALSE, nullptr), CloseHandle);
    if (!event)
    {
        return {L"reply event", GetLastError()};
    }
    OVERLAPPED operation = {};
    operation.hEvent = event.get();
    char request[] = "JYP0 hoenggong";
    char response[256] = {};
    DWORD responseSize = 0;
    if (!TransactNamedPipe(pipe.get(), request, sizeof(request) - 1, response, sizeof(response), &responseSize, &operation))
    {
        status = GetLastError();
        if (status != ERROR_IO_PENDING)
        {
            return {L"transact", status, false, connectMicroseconds};
        }
        DWORD wait = WaitForSingleObject(event.get(), ReplyTimeoutMs);
        if (wait != WAIT_OBJECT_0)
        {
            status = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
            CancelIoEx(pipe.get(), &operation);
            // Drain completion on the worker before releasing OVERLAPPED and its buffers.
            GetOverlappedResult(pipe.get(), &operation, &responseSize, TRUE);
            return {L"wait for reply", status, false, connectMicroseconds};
        }
        if (!GetOverlappedResult(pipe.get(), &operation, &responseSize, FALSE))
        {
            return {L"read reply", GetLastError(), false, connectMicroseconds};
        }
    }

    std::string_view reply(response, responseSize);
    if (reply == "EMPTY")
    {
        return {L"empty", ERROR_SUCCESS, false, connectMicroseconds};
    }
    if (!reply.starts_with("OK\t") || reply.find('\t', 3) == std::string_view::npos ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, response, static_cast<int>(responseSize), nullptr, 0) == 0)
    {
        return {L"decode reply", ERROR_INVALID_DATA, false, connectMicroseconds};
    }
    return {L"reply", ERROR_SUCCESS, true, connectMicroseconds};
#endif
}
#endif

} // namespace

struct IpcProbe::State
{
    struct Result
    {
        uint64_t revision;
        Reply reply;
        Clock::time_point queuedAt;
        Clock::time_point startedAt;
        Clock::time_point completedAt;
        bool typingQuery;
        bool hasQuery;
    };

    std::mutex mutex;
    HWND window = nullptr;
    bool busy = false;
    std::optional<Result> result;
#ifdef JYUTPING_BACKEND_RPC
    BackendRpcClient client;
    BackendSettingsSnapshot settings;
    Clock::time_point nextLaunch{};
#endif
};

struct IpcProbe::Work
{
    std::shared_ptr<State> state;
    uint64_t revision;
    Clock::time_point queuedAt;
#ifdef JYUTPING_RPC_PROBE
    std::wstring spell;
#endif
#ifdef JYUTPING_BACKEND_RPC
    uint64_t generation;
    uint32_t characterStandard;
    BackendLookupMode mode;
    wchar_t trigger;
    bool hasQuery;
    std::vector<MemoryWrite> memoryWrites;
    std::array<std::optional<DWORD>, 8> settingsWrites;
    std::array<unsigned char, 16> settingsInstance;
    uint64_t settingsEpoch;
    bool readSettings;
#endif
};

IpcProbe::~IpcProbe()
{
    if (_window)
    {
        {
            std::lock_guard lock(_state->mutex);
            _state->window = nullptr;
        }
        DestroyWindow(_window);
    }
}

void IpcProbe::Initialize()
{
    if (_window)
    {
        return;
    }
    try
    {
        _state = std::make_shared<State>();
    }
    catch (const std::bad_alloc&)
    {
        return;
    }
    _window = CreateWindowExW(0, L"STATIC", nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, Global::dllInstanceHandle, nullptr);
    if (_window && !SetWindowSubclass(_window, WindowProc, 1, reinterpret_cast<DWORD_PTR>(this)))
    {
        DestroyWindow(_window);
        _window = nullptr;
    }
    _state->window = _window;
    _changedAt = Clock::now();
    WriteProbeLog(_window ? L"Jyutping IPC probe: enabled" : L"Jyutping IPC probe: message window unavailable");
#ifndef JYUTPING_IPC_PROBE_SMOKE
    if (_window)
    {
        // Sample transport availability during activation, before the host receives its first key.
        _queuedAt = Clock::now();
        _pending = true;
        Submit();
    }
#endif
}

void IpcProbe::Invalidate(bool replaceComposition)
{
    ++_revision;
#ifdef JYUTPING_BACKEND_RPC
    if (replaceComposition)
    {
        ++_generation;
    }
#else
    (void)replaceComposition;
#endif
    _pending = false;
    _changedAt = Clock::now();
#ifdef JYUTPING_RPC_PROBE
    _pendingSpell.clear();
    StopSelectionTimer();
#endif
}

#ifdef JYUTPING_RPC_PROBE
void IpcProbe::SetResultCallback(ResultCallback callback, void* context)
{
    _callback = callback;
    _callbackContext = context;
}

void IpcProbe::Query(std::wstring spell, uint32_t characterStandard, BackendLookupMode mode, wchar_t trigger)
{
    _pendingSpell = std::move(spell);
#ifdef JYUTPING_BACKEND_RPC
    _pendingStandard = characterStandard;
    _pendingMode = mode;
    _pendingTrigger = trigger;
#else
    (void)characterStandard;
    (void)mode;
    (void)trigger;
#endif
    _queuedAt = Clock::now();
    _pending = true;
    Submit();
    wchar_t line[180];
    swprintf_s(line, L"Jyutping IPC probe: pid=%lu tid=%lu revision=%llu input_to_submit_us=%lld submit_us=%lld",
        GetCurrentProcessId(), GetCurrentThreadId(), _revision, Microseconds(_queuedAt - _changedAt), Microseconds(Clock::now() - _queuedAt));
    WriteProbeLog(line);
}

bool IpcProbe::ArmSelectionTimeout()
{
    if (!_selectionTimer)
    {
        _selectionDeadline = Clock::now() + SelectionTimeout;
#ifdef JYUTPING_BACKEND_RPC
        if (_nextTimer < SettingsTimerId)
        {
            _nextTimer = SettingsTimerId;
        }
#endif
        _selectionTimer = SetTimer(_window, ++_nextTimer, static_cast<UINT>(SelectionTimeout.count()), nullptr);
    }
    return _selectionTimer != 0;
}

void IpcProbe::StopSelectionTimer()
{
    if (_selectionTimer)
    {
        KillTimer(_window, _selectionTimer);
        _selectionTimer = 0;
    }
}
#endif

void IpcProbe::Measure(Clock::duration localLookup)
{
#ifdef JYUTPING_BACKEND_RPC
    constexpr auto HealthCheckInterval = std::chrono::milliseconds(500);
    const auto now = Clock::now();
    if (now < _nextHealthCheck)
    {
        return;
    }
    _nextHealthCheck = now + HealthCheckInterval;
#endif
#ifdef JYUTPING_RPC_PROBE
    _pendingSpell.clear();
#endif
    _queuedAt = Clock::now();
    _pending = true;
    Submit();

    wchar_t line[200];
    swprintf_s(line, L"Jyutping IPC probe: pid=%lu tid=%lu revision=%llu local_lookup_us=%lld input_to_lookup_us=%lld submit_us=%lld",
        GetCurrentProcessId(), GetCurrentThreadId(), _revision, Microseconds(localLookup), Microseconds(_queuedAt - _changedAt), Microseconds(Clock::now() - _queuedAt));
    WriteProbeLog(line);
}

#ifdef JYUTPING_BACKEND_RPC
void IpcProbe::SetSettingsCallback(SettingsCallback callback)
{
    _settingsCallback = callback;
}

bool IpcProbe::QueueSetting(UINT field, DWORD value)
{
    if (!HasSettings() || field >= _settingsWrites.size())
    {
        return false;
    }
    _settingsWrites[field] = value;
    ++_settingsEpoch;
    _settingsPoll = true;
    Submit();
    return true;
}

bool IpcProbe::QueueMemoryWrite(BackendMemoryOperation operation, const std::wstring& text, const std::wstring& romanization)
{
    constexpr size_t MaxPendingMemoryWrites = 8;
    if (!_memoryEnabled || _memoryWrites.size() >= MaxPendingMemoryWrites)
    {
        return false;
    }
    try
    {
        _memoryWrites.push_back({operation, text, romanization, _generation, _revision, _memoryGeneration, _memoryInstance});
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    Submit();
    return true;
}
#endif

void IpcProbe::Submit()
{
#ifdef JYUTPING_BACKEND_RPC
    if (!_pending && _memoryWrites.empty() && !_settingsPoll)
#else
    if (!_pending)
#endif
    {
        return;
    }
    std::lock_guard lock(_state->mutex);
    if (_state->busy || _state->result)
    {
        return;
    }

    auto work = new (std::nothrow) Work{_state, _revision, _queuedAt
#ifdef JYUTPING_RPC_PROBE
        , std::move(_pendingSpell)
#endif
#ifdef JYUTPING_BACKEND_RPC
        , _generation, _pendingStandard, _pendingMode, _pendingTrigger, _pending, {},
        {}, _settingsSnapshot.instance, _settingsEpoch, _settingsCallback && (_settingsPoll || (_pending && !HasSettings()))
#endif
    };
    if (!work)
    {
        _pending = false;
#ifdef JYUTPING_BACKEND_RPC
        _memoryWrites.clear();
#endif
        return;
    }
#ifdef JYUTPING_BACKEND_RPC
    work->memoryWrites.swap(_memoryWrites);
    work->settingsWrites.swap(_settingsWrites);
    _settingsPoll = false;
#endif
    _pending = false;
    // Outstanding callbacks retain the DLL and their shared state if TSF deactivates first.
    TP_CALLBACK_ENVIRON environment;
    InitializeThreadpoolEnvironment(&environment);
    SetThreadpoolCallbackLibrary(&environment, Global::dllInstanceHandle);
    _state->busy = TrySubmitThreadpoolCallback(Run, work, &environment) != FALSE;
    DestroyThreadpoolEnvironment(&environment);
    if (!_state->busy)
    {
        delete work;
        WriteProbeLog(L"Jyutping IPC probe: could not submit worker");
    }
}

void CALLBACK IpcProbe::Run(PTP_CALLBACK_INSTANCE instance, void* context)
{
    UNREFERENCED_PARAMETER(instance);
    std::unique_ptr<Work> work(static_cast<Work*>(context));
    auto startedAt = Clock::now();
    Reply reply;
    bool typingQuery = false;
    bool hasQuery = true;
    try
    {
#ifdef JYUTPING_BACKEND_RPC
        hasQuery = work->hasQuery;
        for (UINT field = 0; field < work->settingsWrites.size(); ++field)
        {
            if (work->settingsWrites[field])
            {
                const DWORD status = work->state->client.SetSetting(field, *work->settingsWrites[field], work->settingsInstance);
                if (status != ERROR_SUCCESS)
                {
                    wchar_t line[100];
                    swprintf_s(line, L"Jyutping IPC probe: backend setting update failed (%lu)", status);
                    WriteProbeLog(line);
                }
            }
        }
        for (const auto& write : work->memoryWrites)
        {
            const DWORD status = work->state->client.MutateMemory(write.operation, write.text, write.romanization, 1,
                write.generation, write.revision, write.instance, write.memoryGeneration);
            if (status != ERROR_SUCCESS)
            {
                wchar_t line[100];
                swprintf_s(line, L"Jyutping IPC probe: backend memory operation failed (%lu)", status);
                WriteProbeLog(line);
            }
        }
        typingQuery = !work->spell.empty();
        if (hasQuery)
        {
            reply.stage = L"backend RPC";
            reply.error = work->state->client.SuggestCandidates(typingQuery ? std::wstring_view(work->spell) : L"hoenggong",
                typingQuery ? work->mode : BackendLookupMode::Normal, typingQuery ? work->characterStandard : 1, 1, BackendRpcClient::MaxCandidates,
                1, work->generation, work->revision, reply.suggestions, typingQuery ? work->trigger : 0);
            reply.candidate = !reply.suggestions.candidates.empty();
            if (reply.error == RPC_S_SERVER_UNAVAILABLE && Clock::now() >= work->state->nextLaunch)
            {
                work->state->nextLaunch = Clock::now() + std::chrono::seconds(5);
                const DWORD started = LaunchInstalledBackend();
                if (started != ERROR_SUCCESS && started != ERROR_ACCESS_DENIED && started != ERROR_FILE_NOT_FOUND && started != ERROR_PATH_NOT_FOUND)
                {
                    wchar_t line[100];
                    swprintf_s(line, L"Jyutping IPC probe: backend launch failed (%lu)", started);
                    WriteProbeLog(line);
                }
            }
        }
        if (work->readSettings)
        {
            reply.settingsEpoch = work->settingsEpoch;
            const DWORD settingsStatus = work->state->client.GetSettings(work->state->settings);
            if (settingsStatus != ERROR_SUCCESS)
            {
                work->state->settings = {};
            }
            reply.settings = work->state->settings;
        }
#elif defined(JYUTPING_RPC_PROBE)
        typingQuery = !work->spell.empty();
        reply = RequestCandidate(work->spell);
#else
        reply = RequestCandidate();
#endif
    }
    catch (const std::bad_alloc&)
    {
        reply = {L"worker allocation", ERROR_NOT_ENOUGH_MEMORY};
    }
    auto completedAt = Clock::now();
    std::lock_guard lock(work->state->mutex);
    work->state->busy = false;
    if (work->state->window && PostMessageW(work->state->window, ResultMessage, 0, 0))
    {
        work->state->result = State::Result{work->revision, std::move(reply), work->queuedAt, startedAt, completedAt, typingQuery, hasQuery};
    }
}

void IpcProbe::OnResult()
{
    std::optional<State::Result> result;
    {
        std::lock_guard lock(_state->mutex);
        result.swap(_state->result);
    }
    if (result && result->hasQuery)
    {
        wchar_t line[384];
        swprintf_s(line, L"Jyutping IPC probe: pid=%lu tid=%lu revision=%llu current=%d stage=%ls error=%lu candidate=%d queue_us=%lld connect_us=%lld worker_us=%lld dispatch_us=%lld",
            GetCurrentProcessId(), GetCurrentThreadId(), result->revision, result->revision == _revision, result->reply.stage, result->reply.error, result->reply.candidate,
            Microseconds(result->startedAt - result->queuedAt), result->reply.connectMicroseconds, Microseconds(result->completedAt - result->startedAt),
            Microseconds(Clock::now() - result->completedAt));
        WriteProbeLog(line);
    }
    Submit();
#ifdef JYUTPING_RPC_PROBE
    if (result && result->hasQuery)
    {
        _hasListener = result->reply.error == ERROR_SUCCESS;
#ifdef JYUTPING_BACKEND_RPC
        if (_healthTimer)
        {
            KillTimer(_window, _healthTimer);
            _healthTimer = 0;
        }
        if (_hasListener)
        {
            _healthDelayMs = InitialHealthDelayMs;
        }
        else if (result->reply.error == RPC_S_SERVER_UNAVAILABLE || result->reply.error == RPC_S_CALL_FAILED ||
            result->reply.error == RPC_S_CALL_FAILED_DNE || result->reply.error == ERROR_RETRY)
        {
            _healthTimer = SetTimer(_window, HealthTimerId, _healthDelayMs, nullptr);
            _healthDelayMs = (std::min)(_healthDelayMs * 2, MaxHealthDelayMs);
        }
#endif
    }
#endif
#ifdef JYUTPING_BACKEND_RPC
    if (result && result->reply.settings && result->reply.settingsEpoch == _settingsEpoch)
    {
        const bool changed = _settingsSnapshot != *result->reply.settings || _appliedSettingsEpoch != _settingsEpoch;
        _settingsSnapshot = *result->reply.settings;
        if (HasSettings())
        {
            if (!_settingsTimer)
            {
                _settingsTimer = SetTimer(_window, SettingsTimerId, SettingsPollMs, nullptr);
            }
            if (changed)
            {
                _appliedSettingsEpoch = _settingsEpoch;
                _settingsCallback(_callbackContext, _settingsSnapshot);
            }
        }
        else if (_settingsTimer)
        {
            KillTimer(_window, _settingsTimer);
            _settingsTimer = 0;
            Measure(Clock::duration::zero());
        }
    }
#endif
#ifdef JYUTPING_RPC_PROBE
    if (result && result->hasQuery && result->typingQuery && result->revision == _revision && _callback)
    {
        if (_selectionTimer && Clock::now() >= _selectionDeadline)
        {
            result->reply.suggestions.candidates.clear();
        }
#ifdef JYUTPING_BACKEND_RPC
        _memoryEnabled = result->reply.suggestions.memoryEnabled && result->reply.suggestions.memoryGeneration != 0;
        _memoryInstance = result->reply.suggestions.instance;
        _memoryGeneration = result->reply.suggestions.memoryGeneration;
#endif
        StopSelectionTimer();
        _callback(_callbackContext, _revision, std::move(result->reply.suggestions));
    }
#endif
}

LRESULT CALLBACK IpcProbe::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR reference)
{
    if (message == ResultMessage)
    {
        reinterpret_cast<IpcProbe*>(reference)->OnResult();
        return 0;
    }
#ifdef JYUTPING_RPC_PROBE
    auto* probe = reinterpret_cast<IpcProbe*>(reference);
#ifdef JYUTPING_BACKEND_RPC
    if (message == WM_TIMER && probe->_settingsTimer && wParam == probe->_settingsTimer)
    {
        probe->_settingsPoll = true;
        probe->Submit();
        return 0;
    }
    if (message == WM_TIMER && probe->_healthTimer && wParam == probe->_healthTimer)
    {
        KillTimer(window, probe->_healthTimer);
        probe->_healthTimer = 0;
        if (!probe->_hasListener && !probe->_pending)
        {
            probe->Measure(Clock::duration::zero());
        }
        return 0;
    }
#endif
    if (message == WM_TIMER && probe->_selectionTimer && wParam == probe->_selectionTimer && probe->_callback)
    {
        probe->StopSelectionTimer();
        probe->_callback(probe->_callbackContext, probe->_revision, {});
        return 0;
    }
#endif
    if (message == WM_NCDESTROY)
    {
        RemoveWindowSubclass(window, WindowProc, subclassId);
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

#endif
