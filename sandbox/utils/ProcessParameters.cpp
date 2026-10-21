#include "utils/WinAPI.h" /* Must be first include file */
#include "ProcessParameters.hpp"

/*
 * Offset of the working directory inside the process parameters. The member is
 * the `CURDIR` record of the structure, whose `DosPath` string comes first, so
 * the offset is the one of the record: 0x38 in a 64 bit process and 0x24 in a
 * 32 bit one. The structure of the SDK is a partial declaration which stops
 * before the record, so the offset is spelled out like the one of the
 * environment (`utils/GetPEB.cpp`).
 */
#ifdef _WIN64
#define PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET 0x38
#else
#define PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET 0x24
#endif

namespace
{

/** The number of the image name inside an attribute of a creation. */
constexpr ULONG_PTR kAttributeImageName = 5;

/**
 * @brief The record of the working directory of the parameters.
 * @param[in] parameters The process parameters of a call.
 * @return The record, null when the caller passed no parameters.
 */
UNICODE_STRING* CurrentDirectoryOf(PRTL_USER_PROCESS_PARAMETERS parameters)
{
    if (parameters == nullptr)
    {
        return nullptr;
    }

    auto* bytes = reinterpret_cast<BYTE*>(parameters);
    return reinterpret_cast<UNICODE_STRING*>(bytes + PROCESS_PARAMETERS_CURRENT_DIRECTORY_OFFSET);
}

} // namespace

appbox::process_parameters::ImageNameAttribute::ImageNameAttribute(PPS_ATTRIBUTE_LIST attributes)
    : entry_(nullptr), saved_value_(0), saved_size_(0), bSaved_(false)
{
    if (attributes == nullptr || attributes->TotalLength < sizeof(SIZE_T) + sizeof(PS_ATTRIBUTE))
    {
        return;
    }

    /*
     * The walk is bounded by the length the caller declared, so a list which
     * carries fewer entries than it claims is never read past its end.
     */
    const SIZE_T count = (attributes->TotalLength - sizeof(SIZE_T)) / sizeof(PS_ATTRIBUTE);
    for (SIZE_T index = 0; index < count; ++index)
    {
        PS_ATTRIBUTE& attribute = attributes->Attributes[index];
        if ((attribute.Attribute & PS_ATTRIBUTE_NUMBER_MASK) == kAttributeImageName &&
            (attribute.Attribute & PS_ATTRIBUTE_THREAD) == 0)
        {
            entry_ = &attribute;
            return;
        }
    }
}

appbox::process_parameters::ImageNameAttribute::~ImageNameAttribute()
{
    if (!bSaved_ || entry_ == nullptr)
    {
        return;
    }

    entry_->Value = saved_value_;
    entry_->Size = saved_size_;
}

bool appbox::process_parameters::ImageNameAttribute::IsValid() const
{
    return entry_ != nullptr;
}

bool appbox::process_parameters::ImageNameAttribute::GetName(std::wstring& name) const
{
    name.clear();

    if (entry_ == nullptr || entry_->ValuePtr == nullptr || entry_->Size < sizeof(wchar_t) ||
        (entry_->Size % sizeof(wchar_t)) != 0)
    {
        return false;
    }

    /* The buffer is counted, so it is read by its length and never as a text. */
    const auto* text = static_cast<const wchar_t*>(entry_->ValuePtr);
    name.assign(text, entry_->Size / sizeof(wchar_t));
    return true;
}

void appbox::process_parameters::ImageNameAttribute::Set(const std::wstring& path)
{
    if (entry_ == nullptr)
    {
        return;
    }

    if (!bSaved_)
    {
        saved_value_ = entry_->Value;
        saved_size_ = entry_->Size;
        bSaved_ = true;
    }

    text_ = path;
    entry_->ValuePtr = const_cast<wchar_t*>(text_.c_str());
    entry_->Size = text_.size() * sizeof(wchar_t);
}

appbox::process_parameters::ViewParameters::ViewParameters(PRTL_USER_PROCESS_PARAMETERS parameters)
    : parameters_(parameters), saved_image_{}, saved_directory_{}, bImageSet_(false), bDirectorySet_(false)
{
    if (parameters_ == nullptr)
    {
        return;
    }

    saved_image_ = parameters_->ImagePathName;

    if (UNICODE_STRING* directory = CurrentDirectoryOf(parameters_); directory != nullptr)
    {
        saved_directory_ = *directory;
    }
}

appbox::process_parameters::ViewParameters::~ViewParameters()
{
    if (parameters_ == nullptr)
    {
        return;
    }

    if (bImageSet_)
    {
        parameters_->ImagePathName = saved_image_;
    }

    if (bDirectorySet_)
    {
        if (UNICODE_STRING* directory = CurrentDirectoryOf(parameters_); directory != nullptr)
        {
            *directory = saved_directory_;
        }
    }
}

void appbox::process_parameters::ViewParameters::SetImagePath(const std::wstring& path)
{
    if (parameters_ == nullptr || path.empty())
    {
        return;
    }

    image_ = path;
    bImageSet_ = true;

    parameters_->ImagePathName.Buffer = const_cast<PWSTR>(image_.c_str());
    parameters_->ImagePathName.Length = static_cast<USHORT>(image_.size() * sizeof(wchar_t));
    parameters_->ImagePathName.MaximumLength = static_cast<USHORT>((image_.size() + 1) * sizeof(wchar_t));
}

void appbox::process_parameters::ViewParameters::SetCurrentDirectory(const std::wstring& path)
{
    if (parameters_ == nullptr || path.empty())
    {
        return;
    }

    UNICODE_STRING* directory = CurrentDirectoryOf(parameters_);
    if (directory == nullptr)
    {
        return;
    }

    directory_ = path;
    bDirectorySet_ = true;

    directory->Buffer = const_cast<PWSTR>(directory_.c_str());
    directory->Length = static_cast<USHORT>(directory_.size() * sizeof(wchar_t));
    directory->MaximumLength = static_cast<USHORT>((directory_.size() + 1) * sizeof(wchar_t));
}

std::wstring appbox::process_parameters::ToDosPath(const std::wstring& nt_path)
{
    static const wchar_t kNtNamespace[] = L"\\??\\";

    if (nt_path.size() > 4 && nt_path.compare(0, 4, kNtNamespace) == 0)
    {
        return nt_path.substr(4);
    }

    return nt_path;
}

std::wstring appbox::process_parameters::GetCurrentDirectory(PRTL_USER_PROCESS_PARAMETERS parameters)
{
    const UNICODE_STRING* directory = CurrentDirectoryOf(parameters);
    if (directory == nullptr || directory->Buffer == nullptr || directory->Length == 0)
    {
        return {};
    }

    /* The buffer is counted, so it is read by its length and never as a text. */
    return std::wstring(directory->Buffer, static_cast<std::size_t>(directory->Length) / sizeof(wchar_t));
}
