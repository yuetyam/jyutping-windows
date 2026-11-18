#include "IpcProbe.h"

#ifdef JYUTPING_IPC_PROBE

#include "Globals.h"
#include "Logger.h"

#ifdef JYUTPING_RPC_PROBE
#include "../experiments/ipc-probe/appcontainer/RpcProbeClient.h"
#endif
#ifdef JYUTPING_BACKEND_RPC
#include "BackendRpcClient.h"
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

void WriteProbeLog(const wchar_t* line)
{
#ifdef JYUTPING_IPC_PROBE_SMOKE
    OutputDebugStringW(line);
#else
    Global::Log(L"%ls", line);
#endif
}

struct Reply
{
    const wchar_t* stage = L"connect";
    DWORD error = ERROR_SUCCESS;
    bool candidate = false;
    LONGLONG connectMicroseconds = 0;
#ifdef JYUTPING_RPC_PROBE
    RpcProbeCandidate suggestion;
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
    reply.error = QueryRpcProbe(endpoint.c_str(), spell, reply.suggestion);
    reply.candidate = !reply.suggestion.text.empty();
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
    };

    std::mutex mutex;
    HWND window = nullptr;
    bool busy = false;
    std::optional<Result> result;
#ifdef JYUTPING_BACKEND_RPC
    BackendRpcClient client;
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

void IpcProbe::Query(std::wstring spell)
{
    _pendingSpell = std::move(spell);
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

void IpcProbe::Submit()
{
    if (!_pending)
    {
        return;
    }
    std::lock_guard lock(_state->mutex);
    if (_state->busy || _state->result)
    {
        return;
    }

    _pending = false;
    auto work = new (std::nothrow) Work{_state, _revision, _queuedAt
#ifdef JYUTPING_RPC_PROBE
        , std::move(_pendingSpell)
#endif
#ifdef JYUTPING_BACKEND_RPC
        , _generation
#endif
    };
    if (!work)
    {
        return;
    }
    // Outstanding callbacks retain the DLL and their shared state if TSF deactivates first.
    TP_CALLBACK_ENVIRON environment;
    InitializeThreadpoolEnvironment(&environment);
    SetThreadpoolCallbackLibrary(&environment, Global::dllInstanceHandle);
    _state->busy = TrySubmitThreadpoolCallback(Run, work, &environment) != FALSE;
    DestroyThreadpoolEnvironment(&environment);
    if (!_state->busy)
    {
        delete work;
    }
}

void CALLBACK IpcProbe::Run(PTP_CALLBACK_INSTANCE instance, void* context)
{
    UNREFERENCED_PARAMETER(instance);
    std::unique_ptr<Work> work(static_cast<Work*>(context));
    auto startedAt = Clock::now();
    Reply reply;
    bool typingQuery = false;
    try
    {
#ifdef JYUTPING_BACKEND_RPC
        typingQuery = !work->spell.empty();
        BackendRpcCandidate candidate;
        reply.stage = L"backend RPC";
        reply.error = work->state->client.Suggest(typingQuery ? std::wstring_view(work->spell) : L"hoenggong",
            1, work->generation, work->revision, candidate);
        reply.candidate = !candidate.text.empty();
        reply.suggestion = {std::move(candidate.text), std::move(candidate.romanization)};
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
        work->state->result = State::Result{work->revision, std::move(reply), work->queuedAt, startedAt, completedAt, typingQuery};
    }
}

void IpcProbe::OnResult()
{
    std::optional<State::Result> result;
    {
        std::lock_guard lock(_state->mutex);
        result.swap(_state->result);
    }
    if (result)
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
    if (result)
    {
        _hasListener = result->reply.error == ERROR_SUCCESS;
        if (result->typingQuery && result->revision == _revision && _callback)
        {
            if (_selectionTimer && Clock::now() >= _selectionDeadline)
            {
                result->reply.suggestion = {};
            }
            StopSelectionTimer();
            _callback(_callbackContext, _revision, result->reply.suggestion);
        }
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
