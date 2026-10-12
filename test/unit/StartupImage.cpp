#include <gtest/gtest.h>
#include "utils/StartupImage.hpp"
#include "utils/RealFsFolder.hpp"
#include "utils/TestBitmap.hpp"
#include <wincodec.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

/**
 * @brief The interfaces the case uses to write a PNG file.
 *
 * The encoder is the write half of the component the launcher decodes with, so
 * the case produces the file itself instead of carrying a binary one: the
 * interfaces are released in one place, whatever the path through the writer.
 */
struct Encoder
{
    IWICImagingFactory*    factory = nullptr;
    IWICStream*            stream = nullptr;
    IWICBitmapEncoder*     encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    IWICBitmap*            bitmap = nullptr;

    ~Encoder()
    {
        if (frame != nullptr)
        {
            frame->Release();
        }
        if (encoder != nullptr)
        {
            encoder->Release();
        }
        if (stream != nullptr)
        {
            stream->Release();
        }
        if (bitmap != nullptr)
        {
            bitmap->Release();
        }
        if (factory != nullptr)
        {
            factory->Release();
        }
    }

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder() = default;
};

/**
 * @brief The COM apartment the case writes its files in.
 *
 * The writer of the case uses the encoder of the imaging component, which
 * needs an initialized apartment; only an initialization of the case itself is
 * undone again.
 */
class ComApartment
{
public:
    ComApartment()
    {
        owned_ = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) == S_OK;
    }

    ~ComApartment()
    {
        if (owned_)
        {
            CoUninitialize();
        }
    }

    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

private:
    bool owned_ = false;
};

/**
 * @brief Write a PNG file with a single pixel of a known colour.
 *
 * The file is produced by the encoder of the imaging component, so the case
 * reads back a file of the format a user selects in the Settings workspace
 * without carrying one.
 *
 * @param[in] path Path of the file to write.
 * @param[in] width Width of the image.
 * @param[in] height Height of the image.
 * @param[in] red Red value of every pixel.
 * @param[in] alpha Alpha value of every pixel.
 * @return true when the file was written.
 */
bool WritePng(const std::wstring& path, int width, int height, std::uint8_t red, std::uint8_t alpha)
{
    const ComApartment apartment;

    Encoder writer;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&writer.factory))))
    {
        return false;
    }

    if (FAILED(writer.factory->CreateStream(&writer.stream)))
    {
        return false;
    }
    if (FAILED(writer.stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
    {
        return false;
    }

    if (FAILED(writer.factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &writer.encoder)))
    {
        return false;
    }
    if (FAILED(writer.encoder->Initialize(writer.stream, WICBitmapEncoderNoCache)))
    {
        return false;
    }

    IPropertyBag2* options = nullptr;
    if (FAILED(writer.encoder->CreateNewFrame(&writer.frame, &options)))
    {
        return false;
    }
    if (options != nullptr)
    {
        options->Release();
    }
    if (FAILED(writer.frame->Initialize(nullptr)))
    {
        return false;
    }
    if (FAILED(writer.frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height))))
    {
        return false;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(writer.frame->SetPixelFormat(&format)))
    {
        return false;
    }

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4, 0);
    for (std::size_t index = 0; index < pixels.size(); index += 4)
    {
        /* BGRA, with the alpha of the case. */
        pixels[index + 0] = 0;
        pixels[index + 1] = 0;
        pixels[index + 2] = red;
        pixels[index + 3] = alpha;
    }

    const auto stride = static_cast<UINT>(width * 4);
    if (FAILED(writer.factory->CreateBitmapFromMemory(static_cast<UINT>(width), static_cast<UINT>(height),
                                                      GUID_WICPixelFormat32bppBGRA, stride,
                                                      static_cast<UINT>(pixels.size()), pixels.data(), &writer.bitmap)))
    {
        return false;
    }

    if (FAILED(writer.frame->WriteSource(writer.bitmap, nullptr)))
    {
        return false;
    }
    if (FAILED(writer.frame->Commit()))
    {
        return false;
    }

    return SUCCEEDED(writer.encoder->Commit());
}

/**
 * @brief Read one row of a decoded image.
 * @param[in] image Image to read.
 * @param[in] y Row of the image.
 * @param[out] pixel The BGRA value of the first pixel of the row.
 * @return true when the pixel was read.
 */
bool ReadPixel(const appbox::StartupImage& image, int y, std::uint8_t (&pixel)[4])
{
    HDC dc = GetDC(nullptr);
    if (dc == nullptr)
    {
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 1;
    info.bmiHeader.biHeight = 1;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    const bool read = GetDIBits(dc, image.bitmap, static_cast<UINT>(y), 1, pixel, &info, DIB_RGB_COLORS) == 1;

    ReleaseDC(nullptr, dc);
    return read;
}

} // namespace

TEST(Unit_StartupImage, DecodesABitmapFile)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-image-bmp");
    ASSERT_TRUE(appbox::test::WriteTestBitmap(folder.Get() / L"logo.bmp", 4, 3, 0, 0, 0));

    appbox::StartupImage image;
    std::string          error = "not cleared";

    ASSERT_TRUE(appbox::LoadStartupImage((folder.Get() / L"logo.bmp").wstring(), image, error)) << error;
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(image.IsValid());
    EXPECT_EQ(image.width, 4);
    EXPECT_EQ(image.height, 3);
}

TEST(Unit_StartupImage, DecodesAPngFileWithTransparency)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-image-png");
    const auto                       path = (folder.Get() / L"logo.png").wstring();
    ASSERT_TRUE(WritePng(path, 5, 4, 255, 128));

    appbox::StartupImage image;
    std::string          error;
    ASSERT_TRUE(appbox::LoadStartupImage(path, image, error)) << error;

    EXPECT_EQ(image.width, 5);
    EXPECT_EQ(image.height, 4);

    /*
     * The pixels are premultiplied, which is the layout the screen blends
     * with: a red of 255 with an alpha of 128 becomes a red of 128.
     */
    std::uint8_t pixel[4] = {};
    ASSERT_TRUE(ReadPixel(image, 0, pixel));
    EXPECT_EQ(pixel[3], 128);
    EXPECT_GE(pixel[2], 126);
    EXPECT_LE(pixel[2], 129);
}

TEST(Unit_StartupImage, ASecondImageReplacesTheFirst)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-image-replace");

    ASSERT_TRUE(appbox::test::WriteTestBitmap(folder.Get() / L"first.bmp", 2, 2, 0, 0, 0));
    ASSERT_TRUE(appbox::test::WriteTestBitmap(folder.Get() / L"second.bmp", 6, 5, 0, 0, 0));

    appbox::StartupImage image;
    std::string          error;
    ASSERT_TRUE(appbox::LoadStartupImage((folder.Get() / L"first.bmp").wstring(), image, error)) << error;
    EXPECT_EQ(image.width, 2);

    ASSERT_TRUE(appbox::LoadStartupImage((folder.Get() / L"second.bmp").wstring(), image, error)) << error;
    EXPECT_EQ(image.width, 6);
    EXPECT_EQ(image.height, 5);
}

TEST(Unit_StartupImage, RejectsAFileWhichIsNotAnImage)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-image-broken");
    ASSERT_TRUE(folder.WriteFile(L"logo.png", "this is not an image at all"));

    appbox::StartupImage image;
    std::string          error;

    EXPECT_FALSE(appbox::LoadStartupImage((folder.Get() / L"logo.png").wstring(), image, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(image.IsValid());
}

TEST(Unit_StartupImage, RejectsAFileWhichIsNotThere)
{
    const appbox::test::RealFsFolder folder(L"#USERPROFILE#", L"appbox-startup-image-missing");

    appbox::StartupImage image;
    std::string          error;

    EXPECT_FALSE(appbox::LoadStartupImage((folder.Get() / L"missing.png").wstring(), image, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(image.IsValid());
}

TEST(Unit_StartupImage, RejectsAnEmptyPath)
{
    appbox::StartupImage image;
    std::string          error;

    EXPECT_FALSE(appbox::LoadStartupImage(L"", image, error));
    EXPECT_EQ(error, "no startup image was given");
    EXPECT_FALSE(image.IsValid());
}
