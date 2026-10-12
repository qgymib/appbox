#ifndef APPBOX_LAUNCHER_UTILS_STARTUP_IMAGE_HPP
#define APPBOX_LAUNCHER_UTILS_STARTUP_IMAGE_HPP

#include <windows.h>
#include <string>

namespace appbox
{

/**
 * @brief A startup image which was decoded into a bitmap.
 *
 * The bitmap is a device independent bitmap of the screen, so it can be drawn
 * with `AlphaBlend()`: the decoder converts the file into premultiplied BGRA
 * pixels, which keeps the transparency of a PNG logo.
 *
 * The owner is the only one which releases the bitmap, so the record is
 * movable but not copyable.
 */
struct StartupImage
{
    HBITMAP bitmap = nullptr; ///< The decoded bitmap, null while none was loaded.
    int     width = 0;        ///< Width of the bitmap in pixels.
    int     height = 0;       ///< Height of the bitmap in pixels.

    StartupImage() = default;
    ~StartupImage();

    StartupImage(const StartupImage&) = delete;
    StartupImage& operator=(const StartupImage&) = delete;

    /**
     * @brief Take the bitmap of another record.
     * @param[in,out] other Record which gives its bitmap away.
     */
    StartupImage(StartupImage&& other) noexcept;

    /**
     * @brief Replace the bitmap of this record with the one of another record.
     * @param[in,out] other Record which gives its bitmap away.
     * @return This record.
     */
    StartupImage& operator=(StartupImage&& other) noexcept;

    /**
     * @brief Whether a bitmap was decoded.
     * @return true when the record carries an image.
     */
    bool IsValid() const
    {
        return bitmap != nullptr;
    }

    /**
     * @brief Release the bitmap of the record.
     */
    void Reset();
};

/**
 * @brief Decode a startup image file.
 *
 * The file is decoded through the imaging component of Windows, which reads
 * the formats the Settings workspace offers (PNG, JPEG and BMP) without a
 * dependency of the project: the launcher only needs the component which the
 * operating system carries.
 *
 * The call fails without a window, so a caller which has no image to show can
 * continue with the name of the application and the progress bar alone.
 *
 * @param[in] path Path of the image file.
 * @param[out] image The decoded image, untouched when the call fails.
 * @param[out] error Description of the failure, empty when the image was read.
 * @return true when the image was decoded.
 */
bool LoadStartupImage(const std::wstring& path, StartupImage& image, std::string& error);

} // namespace appbox

#endif // APPBOX_LAUNCHER_UTILS_STARTUP_IMAGE_HPP
