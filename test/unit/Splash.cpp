#include <gtest/gtest.h>
#include "Splash.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestBitmap.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Read one pixel of a window.
 * @param[in] window Window to read.
 * @param[in] x Column of the pixel.
 * @param[in] y Row of the pixel.
 * @return The colour of the pixel, `CLR_INVALID` when it cannot be read.
 */
COLORREF ReadWindowPixel(HWND window, int x, int y)
{
    HDC dc = GetDC(window);
    if (dc == nullptr)
    {
        return CLR_INVALID;
    }

    const COLORREF colour = GetPixel(dc, x, y);
    ReleaseDC(window, dc);

    return colour;
}

/**
 * @brief Whether two colours are equal within the tolerance of one channel.
 * @param[in] left First colour.
 * @param[in] right Second colour.
 * @param[in] tolerance Largest difference of one channel.
 * @return true when the colours match.
 */
bool ColoursMatch(COLORREF left, COLORREF right, int tolerance)
{
    if (left == CLR_INVALID || right == CLR_INVALID)
    {
        return false;
    }

    for (int shift = 0; shift <= 16; shift += 8)
    {
        const int a = static_cast<int>((left >> shift) & 0xFF);
        const int b = static_cast<int>((right >> shift) & 0xFF);
        const int difference = a > b ? a - b : b - a;
        if (difference > tolerance)
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Let the screen animate its progress bar.
 *
 * The bar approaches the reported progress on every tick of its timer, so the
 * case pumps the message queue for a while and lets the timer run.
 *
 * @param[in,out] screen Screen to pump.
 * @param[in] rounds Number of rounds to pump.
 */
void PumpFor(appbox::SplashScreen& screen, int rounds)
{
    for (int round = 0; round < rounds; ++round)
    {
        Sleep(20);
        screen.Pump();
    }
}

/**
 * @brief Pump the screen until one of its pixels carries a colour.
 *
 * The bar converges towards the reported progress, so the case waits for the
 * value it reports instead of assuming how many ticks the animation needs.
 *
 * @param[in,out] screen Screen to pump.
 * @param[in] x Column of the pixel.
 * @param[in] y Row of the pixel.
 * @param[in] expected Colour the pixel has to reach.
 * @return true when the pixel reached the colour within the timeout.
 */
bool WaitForPixel(appbox::SplashScreen& screen, int x, int y, COLORREF expected)
{
    for (int attempt = 0; attempt < 250; ++attempt)
    {
        if (ColoursMatch(ReadWindowPixel(screen.Window(), x, y), expected, 8))
        {
            return true;
        }

        Sleep(15);
        screen.Pump();
    }

    return false;
}

} // namespace

TEST(Unit_Splash, TheScreenShowsTheNameTheImageAndTheBar)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-splash-window");
    ASSERT_TRUE(appbox::test::WriteTestBitmap(folder.Get() / L"logo.bmp", 8, 4, 200, 100, 50));

    appbox::SplashScreen screen(true, L"My Application", (folder.Get() / L"logo.bmp").wstring());
    ASSERT_TRUE(screen.IsOpen());

    const HWND window = screen.Window();
    ASSERT_TRUE(IsWindow(window));

    /* The screen is a borderless card of the layout it computed. */
    wchar_t class_name[64] = {};
    ASSERT_GT(GetClassNameW(window, class_name, 64), 0);
    EXPECT_EQ(std::wstring(class_name), L"AppBoxSplash");

    RECT client = {};
    ASSERT_TRUE(GetClientRect(window, &client));
    EXPECT_EQ(client.right - client.left, screen.Layout().width);
    EXPECT_EQ(client.bottom - client.top, screen.Layout().height);

    /* The image is drawn with the size the decoder reported. */
    EXPECT_EQ(screen.Layout().image_width, 8);
    EXPECT_EQ(screen.Layout().image_height, 4);

    /* The name is centered above the image, the bar below it. */
    EXPECT_LT(screen.Layout().name.bottom, screen.Layout().image.top);
    EXPECT_LT(screen.Layout().image.bottom, screen.Layout().bar.top);

    screen.Close();
    EXPECT_FALSE(screen.IsOpen());
    EXPECT_FALSE(IsWindow(window));
}

TEST(Unit_Splash, TheBarAdvancesTowardsTheReportedProgress)
{
    appbox::SplashScreen screen(true, L"My Application", std::wstring());
    ASSERT_TRUE(screen.IsOpen());

    const auto& layout = screen.Layout();

    /* The bar is empty at the beginning of the run. */
    PumpFor(screen, 5);

    const int middle_y = layout.bar.top + layout.bar_height / 2;
    EXPECT_TRUE(ColoursMatch(ReadWindowPixel(screen.Window(), layout.bar.right - 2, middle_y), RGB(226, 226, 230), 8));

    /* The last stage fills it. */
    screen.SetStage(appbox::StartupStage::StartingApplication, 1.0);

    EXPECT_TRUE(WaitForPixel(screen, layout.bar.right - 2, middle_y, RGB(0, 120, 215)));

    screen.Close();
}

TEST(Unit_Splash, AScreenWithoutAnImageKeepsTheNameAndTheBar)
{
    appbox::SplashScreen screen(true, L"My Application", L"");
    ASSERT_TRUE(screen.IsOpen());

    EXPECT_EQ(screen.Layout().image_width, 0);
    EXPECT_EQ(screen.Layout().image_height, 0);

    /* The name and the bar stay in place, so the window is still usable. */
    EXPECT_GT(screen.Layout().name.bottom, 0);
    EXPECT_GT(screen.Layout().bar.bottom, screen.Layout().name.bottom);

    screen.Close();
}

TEST(Unit_Splash, AnImageWhichCannotBeDecodedLeavesTheScreenUsable)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-splash-broken");
    ASSERT_TRUE(folder.WriteFile(L"logo.png", "this is not an image at all"));

    appbox::SplashScreen screen(true, L"My Application", (folder.Get() / L"logo.png").wstring());

    /* The run continues with the name and the progress only. */
    ASSERT_TRUE(screen.IsOpen());
    EXPECT_EQ(screen.Layout().image_width, 0);
    EXPECT_EQ(screen.Layout().image_height, 0);

    screen.Close();
}

TEST(Unit_Splash, AScreenWhichIsTurnedOffIsANoOp)
{
    appbox::SplashScreen screen(false, L"My Application", std::wstring());

    EXPECT_FALSE(screen.IsOpen());
    EXPECT_EQ(screen.Window(), nullptr);

    /* Every method stays harmless while the screen is turned off. */
    screen.SetStage(appbox::StartupStage::LoadingPatches, 0.5);
    screen.Pump();
    screen.Close();

    EXPECT_FALSE(screen.IsOpen());
}

TEST(Unit_Splash, ClosingTwiceKeepsTheScreenClosed)
{
    appbox::SplashScreen screen(true, L"My Application", std::wstring());
    ASSERT_TRUE(screen.IsOpen());

    screen.Close();
    screen.Close();

    EXPECT_FALSE(screen.IsOpen());
    EXPECT_EQ(screen.Window(), nullptr);
}
