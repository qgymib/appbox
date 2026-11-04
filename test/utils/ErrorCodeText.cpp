#include "utils/ErrorCodeText.hpp"
#include <cstdint>
#include <string>

namespace appbox::test
{

/** The reported name of a code, `kUnknown` for a value the table does not name. */
namespace
{

/**
 * @brief Name the code of the table.
 * @param[in] code Code to name.
 * @return The symbolic name of the code.
 */
const char* NameOf(appbox::ErrorCode code) noexcept
{
    switch (code)
    {
    /* General failures. */
    case appbox::ErrorCode::kInvalidArgument:
        return "kInvalidArgument";
    case appbox::ErrorCode::kNullPointer:
        return "kNullPointer";
    case appbox::ErrorCode::kBufferTooSmall:
        return "kBufferTooSmall";
    case appbox::ErrorCode::kNotFound:
        return "kNotFound";
    case appbox::ErrorCode::kAlreadyExists:
        return "kAlreadyExists";
    case appbox::ErrorCode::kAccessDenied:
        return "kAccessDenied";
    case appbox::ErrorCode::kOutOfMemory:
        return "kOutOfMemory";
    case appbox::ErrorCode::kNotSupported:
        return "kNotSupported";
    case appbox::ErrorCode::kInvalidState:
        return "kInvalidState";
    case appbox::ErrorCode::kTimeout:
        return "kTimeout";
    case appbox::ErrorCode::kCancelled:
        return "kCancelled";
    case appbox::ErrorCode::kIoFailure:
        return "kIoFailure";

    /* Text. */
    case appbox::ErrorCode::kTextInputNull:
        return "kTextInputNull";
    case appbox::ErrorCode::kTextConversionFailed:
        return "kTextConversionFailed";

    /* Filesystem and path. */
    case appbox::ErrorCode::kIsolationEntryNotListed:
        return "kIsolationEntryNotListed";
    case appbox::ErrorCode::kLayerKeyUnmapped:
        return "kLayerKeyUnmapped";
    case appbox::ErrorCode::kPathEmpty:
        return "kPathEmpty";
    case appbox::ErrorCode::kPathNoStreamName:
        return "kPathNoStreamName";
    case appbox::ErrorCode::kPathNotInViewNamespace:
        return "kPathNotInViewNamespace";
    case appbox::ErrorCode::kPathNotLocalDrive:
        return "kPathNotLocalDrive";
    case appbox::ErrorCode::kPathVolumeUnmounted:
        return "kPathVolumeUnmounted";
    case appbox::ErrorCode::kPathDeviceUnknown:
        return "kPathDeviceUnknown";
    case appbox::ErrorCode::kPathNotDosDrivePath:
        return "kPathNotDosDrivePath";
    case appbox::ErrorCode::kPathEscapesDriveRoot:
        return "kPathEscapesDriveRoot";
    case appbox::ErrorCode::kKnownFolderUnknown:
        return "kKnownFolderUnknown";
    case appbox::ErrorCode::kKnownFolderUnavailable:
        return "kKnownFolderUnavailable";
    case appbox::ErrorCode::kReparseDataInvalid:
        return "kReparseDataInvalid";
    case appbox::ErrorCode::kReparseTargetNotInView:
        return "kReparseTargetNotInView";
    case appbox::ErrorCode::kReparseDepthExceeded:
        return "kReparseDepthExceeded";

    /* Registry, network and environment. */
    case appbox::ErrorCode::kIsolationTokenUnknown:
        return "kIsolationTokenUnknown";
    case appbox::ErrorCode::kEntryKindTokenUnknown:
        return "kEntryKindTokenUnknown";
    case appbox::ErrorCode::kIsolationDocumentInvalid:
        return "kIsolationDocumentInvalid";
    case appbox::ErrorCode::kIsolationDocumentUnsupportedVersion:
        return "kIsolationDocumentUnsupportedVersion";
    case appbox::ErrorCode::kPortEmpty:
        return "kPortEmpty";
    case appbox::ErrorCode::kPortNotDecimal:
        return "kPortNotDecimal";
    case appbox::ErrorCode::kPortLeadingZero:
        return "kPortLeadingZero";
    case appbox::ErrorCode::kPortOutOfRange:
        return "kPortOutOfRange";
    case appbox::ErrorCode::kAddressMalformed:
        return "kAddressMalformed";

    /* Embedded resources, payload trailer and zip archives. */
    case appbox::ErrorCode::kEmbeddedResourceMissing:
        return "kEmbeddedResourceMissing";
    case appbox::ErrorCode::kEmbeddedResourceEmpty:
        return "kEmbeddedResourceEmpty";
    case appbox::ErrorCode::kEmbeddedResourceLockFailed:
        return "kEmbeddedResourceLockFailed";
    case appbox::ErrorCode::kPayloadTrailerUnreadable:
        return "kPayloadTrailerUnreadable";
    case appbox::ErrorCode::kPayloadTrailerInconsistent:
        return "kPayloadTrailerInconsistent";
    case appbox::ErrorCode::kZipOpenFailed:
        return "kZipOpenFailed";
    case appbox::ErrorCode::kZipCreateDestinationFailed:
        return "kZipCreateDestinationFailed";
    case appbox::ErrorCode::kZipOpenEntryFailed:
        return "kZipOpenEntryFailed";
    case appbox::ErrorCode::kZipReadEntryFailed:
        return "kZipReadEntryFailed";
    case appbox::ErrorCode::kZipEntryNameUnreadable:
        return "kZipEntryNameUnreadable";
    case appbox::ErrorCode::kZipEntryNameUnsafe:
        return "kZipEntryNameUnsafe";
    case appbox::ErrorCode::kZipCreateEntryFailed:
        return "kZipCreateEntryFailed";
    case appbox::ErrorCode::kZipWriteEntryFailed:
        return "kZipWriteEntryFailed";
    case appbox::ErrorCode::kZipCancelled:
        return "kZipCancelled";

    /* PE images. */
    case appbox::ErrorCode::kPeFileUnreadable:
        return "kPeFileUnreadable";
    case appbox::ErrorCode::kPeImageTruncated:
        return "kPeImageTruncated";
    case appbox::ErrorCode::kPeNotAnImage:
        return "kPeNotAnImage";
    case appbox::ErrorCode::kPeInvalidSection:
        return "kPeInvalidSection";
    case appbox::ErrorCode::kPeInvalidExportDirectory:
        return "kPeInvalidExportDirectory";
    case appbox::ErrorCode::kForwarderMalformed:
        return "kForwarderMalformed";
    }

    return "kUnknown";
}

} // namespace

std::string DescribeErrorCode(appbox::ErrorCode code)
{
    return std::string(NameOf(code)) + " (" + std::to_string(static_cast<std::int32_t>(code)) + ")";
}

} // namespace appbox::test
