#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <limits.h>
#include <string.h>

#include "update_check.h"
#include "app_ui.h"
#include "app_version.h"

typedef struct update_check_context {
    HWND target_window;
    char current_version[32];
} update_check_context;

static SRWLOCK update_check_lock = SRWLOCK_INIT;
static HWND update_check_target;
static int update_check_started;
static int update_check_stopping;

static int parse_version(const char *text, unsigned long parts[3])
{
    const char *cursor = text;
    int index;
    if (cursor == NULL)
        return 0;
    if (*cursor == 'v' || *cursor == 'V')
        ++cursor;
    for (index = 0; index < 3; ++index) {
        unsigned long value = 0;
        if (*cursor < '0' || *cursor > '9')
            return 0;
        do {
            unsigned int digit = (unsigned int)(*cursor - '0');
            if (value > (ULONG_MAX - digit) / 10)
                return 0;
            value = value * 10 + digit;
            ++cursor;
        } while (*cursor >= '0' && *cursor <= '9');
        parts[index] = value;
        if (index != 2) {
            if (*cursor != '.')
                return 0;
            ++cursor;
        }
    }
    /* Compare stable numeric release tags only; ignore malformed/prerelease tags. */
    return *cursor == '\0';
}

static int version_is_newer(const char *candidate, const char *current)
{
    unsigned long newer[3], installed[3];
    int index;
    if (!parse_version(candidate, newer) || !parse_version(current, installed))
        return 0;
    for (index = 0; index < 3; ++index) {
        if (newer[index] != installed[index])
            return newer[index] > installed[index];
    }
    return 0;
}

static int read_release_tag(const char *json, char tag[64])
{
    const char *field = strstr(json, "\"tag_name\"");
    const char *cursor;
    size_t length = 0;
    if (field == NULL)
        return 0;
    cursor = strchr(field, ':');
    if (cursor == NULL)
        return 0;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n')
        ++cursor;
    if (*cursor++ != '"')
        return 0;
    while (cursor[length] != '\0' && cursor[length] != '"' && length < 63) {
        if (cursor[length] == '\\')
            return 0;
        ++length;
    }
    if (length == 0 || cursor[length] != '"')
        return 0;
    memcpy(tag, cursor, length);
    tag[length] = '\0';
    return 1;
}

static DWORD WINAPI update_check_thread(void *parameter)
{
    update_check_context *context = (update_check_context *)parameter;
    HINTERNET session = NULL, connection = NULL, request = NULL;
    char response[65536];
    char tag[64];
    DWORD used = 0;
    DWORD timeout = 4000;
    response[0] = '\0';
    session = WinHttpOpen(L"OffsetPad/" OFFSET_PAD_VERSION_W,
                          WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL)
        goto done;
    WinHttpSetTimeouts(session, (int)timeout, (int)timeout, (int)timeout, (int)timeout);
    connection = WinHttpConnect(session, L"gitee.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == NULL)
        goto done;
    request = WinHttpOpenRequest(connection, L"GET",
        L"/api/v5/repos/OrlandoHsu29/offset-pad/releases/latest", NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == NULL)
        goto done;
    if (!WinHttpAddRequestHeaders(request,
        L"Accept: application/json\r\n",
        (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL))
        goto done;
    {
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 NULL, &status, &status_size, NULL) || status != 200)
            goto done;
    }
    for (;;) {
        DWORD available = 0;
        DWORD received = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            goto done;
        if (available == 0)
            break;
        if (available > sizeof(response) - 1 - used)
            goto done;
        if (!WinHttpReadData(request, response + used, available, &received) || received == 0)
            goto done;
        used += received;
        response[used] = '\0';
    }
    if (read_release_tag(response, tag) && version_is_newer(tag, context->current_version)) {
        wchar_t *version = (wchar_t *)HeapAlloc(GetProcessHeap(), 0, 64 * sizeof(wchar_t));
        if (version != NULL) {
            int chars = MultiByteToWideChar(CP_UTF8, 0, tag, -1, version, 64);
            int posted = 0;
            if (chars > 0) {
                AcquireSRWLockExclusive(&update_check_lock);
                if (!update_check_stopping &&
                    PostMessageW(context->target_window, WM_OFFSET_PAD_UPDATE_AVAILABLE,
                                 (WPARAM)version, 0))
                    posted = 1;
                ReleaseSRWLockExclusive(&update_check_lock);
            }
            if (posted)
                version = NULL;
            if (version != NULL)
                HeapFree(GetProcessHeap(), 0, version);
        }
    }
done:
    if (request != NULL) WinHttpCloseHandle(request);
    if (connection != NULL) WinHttpCloseHandle(connection);
    if (session != NULL) WinHttpCloseHandle(session);
    HeapFree(GetProcessHeap(), 0, context);
    return 0;
}

void update_check_start(HWND target_window, const char *current_version)
{
    update_check_context *context;
    HANDLE thread;
    if (target_window == NULL || current_version == NULL)
        return;
    context = (update_check_context *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*context));
    if (context == NULL)
        return;
    AcquireSRWLockExclusive(&update_check_lock);
    if (update_check_started) {
        ReleaseSRWLockExclusive(&update_check_lock);
        HeapFree(GetProcessHeap(), 0, context);
        return;
    }
    update_check_started = 1;
    update_check_stopping = 0;
    update_check_target = target_window;
    ReleaseSRWLockExclusive(&update_check_lock);
    context->target_window = target_window;
    lstrcpynA(context->current_version, current_version, sizeof(context->current_version));
    thread = CreateThread(NULL, 0, update_check_thread, context, 0, NULL);
    if (thread == NULL) {
        AcquireSRWLockExclusive(&update_check_lock);
        update_check_stopping = 1;
        update_check_target = NULL;
        ReleaseSRWLockExclusive(&update_check_lock);
        HeapFree(GetProcessHeap(), 0, context);
        return;
    }
    CloseHandle(thread);
}

void update_check_stop(void)
{
    HWND target;
    MSG message;
    AcquireSRWLockExclusive(&update_check_lock);
    update_check_stopping = 1;
    target = update_check_target;
    update_check_target = NULL;
    ReleaseSRWLockExclusive(&update_check_lock);
    if (target == NULL)
        return;
    while (PeekMessageW(&message, target, WM_OFFSET_PAD_UPDATE_AVAILABLE,
                        WM_OFFSET_PAD_UPDATE_AVAILABLE, PM_REMOVE))
        HeapFree(GetProcessHeap(), 0, (void *)message.wParam);
}
