#include "bindings/MouseHotkeyWatcher.h"

#include "bindings/MousePressQueue.h"

#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace undek::bindings {
namespace {

std::mutex g_mutex;
MousePressQueue g_queue;
std::atomic<bool> g_running{false};

#ifdef _WIN32

std::thread g_thread;
std::atomic<unsigned long> g_thread_id{0};
std::atomic<HHOOK> g_hook{nullptr};
HANDLE g_wake = nullptr;

unsigned int heldModifiers() {
    unsigned int mods = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) mods |= modifier_bit::kControl;
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) mods |= modifier_bit::kShift;
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) mods |= modifier_bit::kAlt;
    return mods;
}

void queuePress(unsigned int vk) {
    const unsigned int mods = heldModifiers();
    int hit = 0;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        hit = g_queue.match(vk, mods);
        if (hit == 0) return;
        // A run can take a while; don't hold a backlog of presses nobody asked for.
        g_queue.push(hit);
    }
    if (g_wake) SetEvent(g_wake);
}

LRESULT CALLBACK mouseHookProc(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && g_running.load()) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
        // Our own injected side clicks come through here too, and must not retrigger.
        if ((ms->flags & LLMHF_INJECTED) == 0) {
            unsigned int vk = 0;
            if (MousePressQueue::buttonFor(static_cast<unsigned int>(wparam),
                                           HIWORD(ms->mouseData) == XBUTTON2, vk)) {
                // Every press is passed on. Eating one would also eat the
                // macro's own presses when they come from the Interception
                // driver, which arrives looking exactly like hardware.
                queuePress(vk);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

void hookThreadBody() {
    g_thread_id = GetCurrentThreadId();
    const HHOOK hook = SetWindowsHookExA(WH_MOUSE_LL, mouseHookProc, GetModuleHandleA(nullptr), 0);
    if (!hook) {
        g_running = false;
        return;
    }
    g_hook = hook;

    // Low level hooks arrive through this thread's queue, so it has to keep turning.
    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
    }

    UnhookWindowsHookEx(hook);
    g_hook = nullptr;
    g_running = false;
}

#endif

} // namespace

bool MouseHotkeyWatcher::start(const std::vector<WatchedButton>& buttons, std::string& error_out) {
    stop();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_queue.setButtons(buttons);
    }
    if (buttons.empty()) return true;

#ifdef _WIN32
    g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_running = true;
    g_thread = std::thread(hookThreadBody);

    // Wait for the hook to install, so a refusal is reported instead of leaving
    // the side buttons silently dead.
    for (int i = 0; i < 50 && g_running.load() && g_hook.load() == nullptr; ++i) {
        Sleep(2);
    }

    if (g_hook.load() == nullptr) {
        error_out = "could not install the mouse hook";
        stop();
        return false;
    }
    return true;
#else
    (void)error_out;
    return false;
#endif
}

void MouseHotkeyWatcher::stop() {
#ifdef _WIN32
    if (g_thread.joinable()) {
        if (g_thread_id.load() != 0) PostThreadMessageA(g_thread_id.load(), WM_QUIT, 0, 0);
        g_thread.join();
    }
    g_thread_id = 0;
    g_hook = nullptr;
    if (g_wake) {
        CloseHandle(g_wake);
        g_wake = nullptr;
    }
#endif
    g_running = false;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.clear();
}

bool MouseHotkeyWatcher::running() {
    return g_running.load();
}

std::vector<int> MouseHotkeyWatcher::takePressedIds() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_queue.take();
}

void* MouseHotkeyWatcher::wakeHandle() {
#ifdef _WIN32
    return g_wake;
#else
    return nullptr;
#endif
}

} // namespace undek::bindings
