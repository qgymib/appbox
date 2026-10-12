#include "StartupImage.hpp"
#include <wincodec.h>
#include <cstddef>
#include <string>
#include <utility>

namespace
{

/**
 * @brief Release a COM interface when the holder goes out of scope.
 *
 * The decoder works with the interfaces of the imaging component of Windows,
 * which have to be released on every path; the holder keeps the release in one
 * place instead of repeating it after every call.
 *
 * @tparam T Interface to hold.
 */
template <typename T>
class ComHolder
{
public:
    ComHolder() = default;
    ~ComHolder()
    {
        Reset();
    }

    ComHolder(const ComHolder&) = delete;
    ComHolder& operator=(const ComHolder&) = delete;

    /**
     * @brief Get the interface.
     * @return The interface, null while none is held.
     */
    T* Get() const
    {
        return pointer_;
    }

    /**
     * @brief Get the room of an out parameter, which drops the held interface.
     * @return The address the callee writes the interface to.
     */
    T** Put()
    {
        Reset();
        return &pointer_;
    }

    /**
     * @brief Call a method of the interface.
     * @return The interface.
     */
    T* operator->() const
    {
        return pointer_;
    }

private:
    /**
     * @brief Release the held interface.
     */
    void Reset()
    {
        if (pointer_ != nullptr)
        {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

    T* pointer_ = nullptr;
};

/**
 * @brief Format a result code as hexadecimal text.
 * @param[in] value Result code to format.
 * @return The code, e.g. `80070002`.
 */
std::string Hex(HRESULT value)
{
    static const char* const digits = "0123456789ABCDEF";

    const auto raw = static_cast<unsigned long>(value);

    std::string text;
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        text.push_back(digits[(raw >> shift) & 0xF]);
    }

    return text;
}

/**
 * @brief The COM apartment the decoder runs in.
 *
 * The launcher is no COM application, so the decoder initializes the apartment
 * of the calling thread. A thread which is initialized already keeps its own
 * apartment, which is enough for the imaging component; only an initialization
 * this object performed itself is undone again.
 */
class ComApartment
{
public:
    ComApartment()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (result == S_OK)
        {
            owned_ = true;
            return;
        }

        /*
         * S_FALSE means the thread is initialized already, and
         * RPC_E_CHANGED_MODE means it is initialized with another threading
         * model; the imaging component works in either case.
         */
        if (result == S_FALSE || result == RPC_E_CHANGED_MODE)
        {
            return;
        }

        usable_ = false;
        error_ = "the imaging component of Windows is unavailable: 0x" + Hex(result);
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

    /**
     * @brief Whether COM is usable on this thread.
     * @return true when the decoder may run.
     */
    bool IsUsable() const
    {
        return usable_;
    }

    /**
     * @brief Get the reason why COM is not usable.
     * @return The description, empty while COM is usable.
     */
    const std::string& Error() const
    {
        return error_;
    }

private:
    bool        owned_ = false;
    bool        usable_ = true;
    std::string error_;
};

/**
 * @brief Decode an image file into a device independent bitmap.
 * @param[in] path Path of the image file.
 * @param[out] image The decoded image.
 * @param[out] error Description of the failure, empty on success.
 * @return true when the image was decoded.
 */
bool DecodeImage(const std::wstring& path, appbox::StartupImage& image, std::string& error)
{
    ComHolder<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.Put()));
    if (FAILED(hr))
    {
        error = "the imaging component of Windows cannot be created: 0x" + Hex(hr);
        return false;
    }

    ComHolder<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                            decoder.Put());
    if (FAILED(hr))
    {
        error = "the startup image cannot be read: 0x" + Hex(hr);
        return false;
    }

    ComHolder<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.Put());
    if (FAILED(hr))
    {
        error = "the startup image has no frame: 0x" + Hex(hr);
        return false;
    }

    /*
     * The converter produces premultiplied BGRA pixels whatever the file
     * carries, which is the layout `AlphaBlend()` expects: a logo with
     * transparency keeps it and a file without an alpha channel is opaque.
     */
    ComHolder<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.Put());
    if (FAILED(hr))
    {
        error = "the pixel format of the startup image cannot be converted: 0x" + Hex(hr);
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        error = "the pixel format of the startup image cannot be converted: 0x" + Hex(hr);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        error = "the startup image carries no pixels";
        return false;
    }

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    /* A negative height makes the rows run top down, which is the order the decoder writes them in. */
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void*   bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr)
    {
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }
        error = "the startup image cannot be drawn: no memory for its pixels";
        return false;
    }

    const UINT stride = width * 4;
    hr = converter->CopyPixels(nullptr, stride, stride * height, static_cast<BYTE*>(bits));
    if (FAILED(hr))
    {
        DeleteObject(bitmap);
        error = "the pixels of the startup image cannot be read: 0x" + Hex(hr);
        return false;
    }

    image.Reset();
    image.bitmap = bitmap;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    return true;
}

} // namespace

namespace appbox
{

StartupImage::~StartupImage()
{
    Reset();
}

StartupImage::StartupImage(StartupImage&& other) noexcept
{
    bitmap = other.bitmap;
    width = other.width;
    height = other.height;

    other.bitmap = nullptr;
    other.width = 0;
    other.height = 0;
}

StartupImage& StartupImage::operator=(StartupImage&& other) noexcept
{
    if (this != &other)
    {
        Reset();

        bitmap = other.bitmap;
        width = other.width;
        height = other.height;

        other.bitmap = nullptr;
        other.width = 0;
        other.height = 0;
    }

    return *this;
}

void StartupImage::Reset()
{
    if (bitmap != nullptr)
    {
        DeleteObject(bitmap);
        bitmap = nullptr;
    }

    width = 0;
    height = 0;
}

bool LoadStartupImage(const std::wstring& path, StartupImage& image, std::string& error)
{
    error.clear();

    if (path.empty())
    {
        error = "no startup image was given";
        return false;
    }

    const ComApartment apartment;
    if (!apartment.IsUsable())
    {
        error = apartment.Error();
        return false;
    }

    return DecodeImage(path, image, error);
}

} // namespace appbox
