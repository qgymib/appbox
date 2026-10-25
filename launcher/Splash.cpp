#include <spdlog/spdlog.h>
#include "Splash.hpp"

namespace
{

/** Name of the window class of the screen. */
const wchar_t* const kSplashClassName = L"AppBoxSplash";

/** Identifier of the timer which animates the progress bar. */
constexpr UINT_PTR kSplashTimerId = 1;

/** Interval of the timer which animates the progress bar, in milliseconds. */
constexpr UINT kSplashTimerInterval = 33;

/** Smallest step the bar takes towards the reported progress. */
constexpr double kSplashMinStep = 0.004;

/** Size of the name of the application, in pixels at 96 DPI. */
constexpr int kSplashNamePixels = 16;

/** Colour of the card the screen draws. */
constexpr COLORREF kSplashBackground = RGB(250, 250, 250);

/** Colour of the border of the card. */
constexpr COLORREF kSplashBorder = RGB(202, 202, 206);

/** Colour of the name of the application. */
constexpr COLORREF kSplashNameColour = RGB(32, 32, 32);

/** Colour of the empty part of the progress bar. */
constexpr COLORREF kSplashTrackColour = RGB(226, 226, 230);

/** Colour of the filled part of the progress bar. */
constexpr COLORREF kSplashFillColour = RGB(0, 120, 215);

/**
 * @brief Get the resolution of the primary monitor.
 *
 * The launcher carries no manifest which would make it aware of the resolution
 * of the monitor, so the system reports the resolution it scales the user
 * interface of the process with. The layout is therefore spelled in the units
 * the system scales, which keeps the screen in proportion on every monitor.
 *
 * @return The resolution in dots per inch, at least 96.
 */
unsigned int SystemDpi()
{
    HDC dc = GetDC(nullptr);
    if (dc == nullptr)
    {
        return 96;
    }

    const int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);

    return dpi > 0 ? static_cast<unsigned int>(dpi) : 96;
}

/**
 * @brief Get the work area the screen is centered in.
 *
 * The screen appears on the monitor of the window which has the focus, which is
 * where the user started the packaged application; a machine without such a
 * window falls back to the work area of the primary monitor.
 *
 * @return The work area of the monitor.
 */
RECT WorkArea()
{
    MONITORINFO info = {};
    info.cbSize = sizeof(info);

    const HMONITOR monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &info))
    {
        return info.rcWork;
    }

    RECT area = {};
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0))
    {
        return area;
    }

    area.right = GetSystemMetrics(SM_CXSCREEN);
    area.bottom = GetSystemMetrics(SM_CYSCREEN);
    return area;
}

/**
 * @brief Create the font the name of the application is drawn with.
 *
 * The font is the message font of the system, which is the font the shell uses
 * for the text of a window, scaled to the resolution of the monitor.
 *
 * @param[in] dpi Resolution of the monitor.
 * @return The font, null when it cannot be created.
 */
HFONT CreateNameFont(unsigned int dpi)
{
    LOGFONTW log_font = {};

    NONCLIENTMETRICSW metrics = {};
    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
    {
        log_font = metrics.lfMessageFont;
    }
    else
    {
        log_font.lfWeight = FW_NORMAL;
        log_font.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(log_font.lfFaceName, L"Segoe UI");
    }

    log_font.lfHeight = -MulDiv(kSplashNamePixels, static_cast<int>(dpi), 96);
    log_font.lfWeight = FW_SEMIBOLD;
    log_font.lfQuality = CLEARTYPE_QUALITY;

    return CreateFontIndirectW(&log_font);
}

/**
 * @brief Measure the room the name of the application needs.
 * @param[in] font Font the name is drawn with.
 * @return The height of one line of text, zero when it cannot be measured.
 */
int MeasureNameHeight(HFONT font)
{
    HDC dc = GetDC(nullptr);
    if (dc == nullptr)
    {
        return 0;
    }

    const HGDIOBJ old_font = SelectObject(dc, font);

    TEXTMETRICW metrics = {};
    const bool  measured = GetTextMetricsW(dc, &metrics) != FALSE;

    SelectObject(dc, old_font);
    ReleaseDC(nullptr, dc);

    return measured ? static_cast<int>(metrics.tmHeight) : 0;
}

} // namespace

namespace appbox
{

SplashScreen::SplashScreen(bool enabled, const std::wstring& display_name, std::string_view image_data)
{
    if (!enabled)
    {
        /* The session turned the screen off, so there is nothing to show. */
        return;
    }

    name_ = display_name;

    if (!image_data.empty())
    {
        std::string error;
        if (!LoadStartupImage(image_data, image_, error))
        {
            /*
             * The image is a decoration of the screen: an image resource which
             * cannot be decoded leaves the name and the progress in place
             * instead of failing the run.
             */
            SPDLOG_WARN("the startup screen is shown without its image: {}", error);
        }
    }

    if (!CreateWindowHandle())
    {
        SPDLOG_WARN("the startup screen cannot be shown");
        ReleaseResources();
        return;
    }

    SPDLOG_INFO("the startup screen is shown ({}x{}, image {}x{})", layout_.width, layout_.height, image_.width,
                image_.height);

    SetTimer(window_, kSplashTimerId, kSplashTimerInterval, nullptr);
    Pump();
}

SplashScreen::~SplashScreen()
{
    Close();
}

bool SplashScreen::CreateWindowHandle()
{
    WNDCLASSEXW description = {};
    description.cbSize = sizeof(description);
    description.style = CS_HREDRAW | CS_VREDRAW;
    description.lpfnWndProc = &SplashScreen::WindowProc;
    description.hInstance = GetModuleHandleW(nullptr);
    /* The cursor of the system is a resource identifier and not a string, so the cast is explicit. */
    description.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
    description.lpszClassName = kSplashClassName;

    if (RegisterClassExW(&description) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        SPDLOG_ERROR("the window class of the startup screen cannot be registered: {}", GetLastError());
        return false;
    }

    const unsigned int dpi = SystemDpi();
    font_ = CreateNameFont(dpi);

    const RECT work_area = WorkArea();
    layout_ = ComputeStartupLayout(image_.width, image_.height, MeasureNameHeight(font_), work_area, dpi);

    const int left = work_area.left + (work_area.right - work_area.left - layout_.width) / 2;
    const int top = work_area.top + (work_area.bottom - work_area.top - layout_.height) / 2;

    /*
     * The screen is a popup without a border, so the window is exactly the
     * card it draws, and it is a tool window, so it never appears in the task
     * bar or in the list of the windows a user can switch to.
     */
    window_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSplashClassName, name_.c_str(), WS_POPUP, left, top, layout_.width,
                              layout_.height, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (window_ == nullptr)
    {
        SPDLOG_ERROR("the window of the startup screen cannot be created: {}", GetLastError());
        return false;
    }

    CreateResources();

    /* The screen never takes the focus from the program the run starts. */
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    UpdateWindow(window_);
    return true;
}

void SplashScreen::CreateResources()
{
    background_ = CreateSolidBrush(kSplashBackground);
    border_ = CreateSolidBrush(kSplashBorder);
    track_ = CreateSolidBrush(kSplashTrackColour);
    fill_ = CreateSolidBrush(kSplashFillColour);

    /*
     * The screen is composed in a bitmap of its own and copied to the window
     * in one step, so a repaint never flickers.
     */
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc != nullptr)
    {
        buffer_dc_ = CreateCompatibleDC(screen_dc);
        buffer_bitmap_ = CreateCompatibleBitmap(screen_dc, layout_.width, layout_.height);
        ReleaseDC(nullptr, screen_dc);
    }

    if (buffer_dc_ != nullptr && buffer_bitmap_ != nullptr)
    {
        old_buffer_bitmap_ = SelectObject(buffer_dc_, buffer_bitmap_);
    }

    if (image_.IsValid())
    {
        image_dc_ = CreateCompatibleDC(nullptr);
        if (image_dc_ != nullptr)
        {
            SelectObject(image_dc_, image_.bitmap);
        }
    }
}

void SplashScreen::ReleaseResources()
{
    if (old_buffer_bitmap_ != nullptr && buffer_dc_ != nullptr)
    {
        SelectObject(buffer_dc_, old_buffer_bitmap_);
        old_buffer_bitmap_ = nullptr;
    }

    if (buffer_dc_ != nullptr)
    {
        DeleteDC(buffer_dc_);
        buffer_dc_ = nullptr;
    }

    if (image_dc_ != nullptr)
    {
        DeleteDC(image_dc_);
        image_dc_ = nullptr;
    }

    if (buffer_bitmap_ != nullptr)
    {
        DeleteObject(buffer_bitmap_);
        buffer_bitmap_ = nullptr;
    }

    const HGDIOBJ objects[] = { font_, background_, border_, track_, fill_ };
    for (auto* object : objects)
    {
        if (object != nullptr)
        {
            DeleteObject(object);
        }
    }

    font_ = nullptr;
    background_ = nullptr;
    border_ = nullptr;
    track_ = nullptr;
    fill_ = nullptr;
}

void SplashScreen::SetStage(StartupStage stage, double fraction)
{
    if (window_ == nullptr)
    {
        return;
    }

    target_ = StartupStageProgress(stage, fraction);
    Pump();
}

void SplashScreen::Pump()
{
    if (window_ == nullptr)
    {
        return;
    }

    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            /*
             * The launcher owns no message loop which would consume the
             * request, so it is put back and the pumping stops.
             */
            PostQuitMessage(static_cast<int>(message.wParam));
            return;
        }

        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void SplashScreen::Close()
{
    if (window_ != nullptr)
    {
        KillTimer(window_, kSplashTimerId);

        /*
         * Destroying the window sends WM_NCDESTROY, which clears the address
         * the window holds, so the screen is closed before the resources go.
         */
        DestroyWindow(window_);
        window_ = nullptr;

        SPDLOG_INFO("the startup screen is closed");
    }

    ReleaseResources();
}

void SplashScreen::AdvanceProgress()
{
    if (shown_ >= target_)
    {
        return;
    }

    /* The bar approaches the reported progress, so it never jumps. */
    const double remaining = target_ - shown_;
    const double step = remaining * 0.25;
    shown_ += step > kSplashMinStep ? step : kSplashMinStep;
    if (shown_ > target_)
    {
        shown_ = target_;
    }

    InvalidateRect(window_, &layout_.bar, FALSE);
}

void SplashScreen::Paint()
{
    PAINTSTRUCT paint = {};
    HDC         window_dc = BeginPaint(window_, &paint);
    if (window_dc == nullptr)
    {
        return;
    }

    if (buffer_dc_ != nullptr)
    {
        RECT card = { 0, 0, layout_.width, layout_.height };
        FillRect(buffer_dc_, &card, background_);
        FrameRect(buffer_dc_, &card, border_);

        if (font_ != nullptr)
        {
            const HGDIOBJ old_font = SelectObject(buffer_dc_, font_);
            SetBkMode(buffer_dc_, TRANSPARENT);
            SetTextColor(buffer_dc_, kSplashNameColour);

            /* DT_NOPREFIX keeps an ampersand of a product name as it is. */
            DrawTextW(buffer_dc_, name_.c_str(), -1, &layout_.name,
                      DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

            SelectObject(buffer_dc_, old_font);
        }

        if (image_.IsValid() && image_dc_ != nullptr)
        {
            BLENDFUNCTION blend = {};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;

            const int width = layout_.image.right - layout_.image.left;
            const int height = layout_.image.bottom - layout_.image.top;

            AlphaBlend(buffer_dc_, layout_.image.left, layout_.image.top, width, height, image_dc_, 0, 0, image_.width,
                       image_.height, blend);
        }

        FillRect(buffer_dc_, &layout_.bar, track_);

        const int filled = static_cast<int>((layout_.bar.right - layout_.bar.left) * shown_);
        if (filled > 0)
        {
            RECT progress = layout_.bar;
            progress.right = progress.left + filled;
            FillRect(buffer_dc_, &progress, fill_);
        }

        BitBlt(window_dc, 0, 0, layout_.width, layout_.height, buffer_dc_, 0, 0, SRCCOPY);
    }

    EndPaint(window_, &paint);
}

LRESULT SplashScreen::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_ERASEBKGND:
        /* The card covers the whole window, so nothing has to be erased. */
        return 1;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_TIMER:
        if (wparam == kSplashTimerId)
        {
            AdvanceProgress();
            return 0;
        }
        break;

    case WM_NCDESTROY:
        /* The window is gone, so the screen stops referring to it. */
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        window_ = nullptr;
        return 0;

    default:
        break;
    }

    return DefWindowProcW(window_, message, wparam, lparam);
}

LRESULT CALLBACK SplashScreen::WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE)
    {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        auto*       screen = static_cast<SplashScreen*>(create->lpCreateParams);

        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(screen));
        screen->window_ = window;
        return TRUE;
    }

    auto* screen = reinterpret_cast<SplashScreen*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (screen == nullptr)
    {
        return DefWindowProcW(window, message, wparam, lparam);
    }

    return screen->HandleMessage(message, wparam, lparam);
}

} // namespace appbox
