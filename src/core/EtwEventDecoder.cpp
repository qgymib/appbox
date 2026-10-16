#include "EtwEventDecoder.hpp"

#include <tdh.h>
#include <algorithm>
#include <climits>
#include <cstring>
#include <utility>

namespace
{

/**
 * @brief Number of metadata buffers the decoder keeps.
 *
 * The capture subscribes to two providers which declare a few dozen events, so
 * the cache holds a handful of entries; the bound is what keeps a decoder
 * which is fed with the events of other providers from growing without a
 * limit.
 */
constexpr std::size_t kMaxMetadataEntries = 64;

/** The size the metadata of an event is asked for with. */
constexpr std::size_t kInitialMetadataSize = 4096;

/** The types TDH reports for the properties of an event. */
constexpr USHORT kIntypeUnicodeString = 1;
constexpr USHORT kIntypeAnsiString = 2;
constexpr USHORT kIntypeInt8 = 3;
constexpr USHORT kIntypeUInt8 = 4;
constexpr USHORT kIntypeInt16 = 5;
constexpr USHORT kIntypeUInt16 = 6;
constexpr USHORT kIntypeInt32 = 7;
constexpr USHORT kIntypeUInt32 = 8;
constexpr USHORT kIntypeInt64 = 9;
constexpr USHORT kIntypeUInt64 = 10;
constexpr USHORT kIntypeFloat = 11;
constexpr USHORT kIntypeDouble = 12;
constexpr USHORT kIntypeBoolean = 13;
constexpr USHORT kIntypeGuid = 15;
constexpr USHORT kIntypePointer = 16;
constexpr USHORT kIntypeFileTime = 17;
constexpr USHORT kIntypeSystemTime = 18;
constexpr USHORT kIntypeHexInt32 = 20;
constexpr USHORT kIntypeHexInt64 = 21;
constexpr USHORT kIntypeCountedString = 22;
constexpr USHORT kIntypeCountedAnsiString = 23;

/** The names of the properties which carry the object of an event. */
constexpr const wchar_t* const kObjectNames[] = { L"FileObject", L"KeyObject", L"KeyHandle" };

/** The names of the properties which carry the file key of an event. */
constexpr const wchar_t* const kFileKeyNames[] = { L"FileKey" };

/** The names of the properties which carry the name of an event. */
constexpr const wchar_t* const kNameNames[] = { L"FileName", L"FilePath", L"KeyName" };

/**
 * @brief Whether a text of a list of names matches a property name.
 * @param[in] name Name of the property.
 * @param[in] names The names to compare with.
 * @return true when the name is one of the names.
 */
template <std::size_t size>
bool IsNamed(const wchar_t* name, const wchar_t* const (&names)[size])
{
    for (const auto* candidate : names)
    {
        if (wcscmp(name, candidate) == 0)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Whether a property is a text TDH formats.
 * @param[in] type The type of the property.
 * @return true when the property is a text.
 */
bool IsText(USHORT type)
{
    return type == kIntypeUnicodeString || type == kIntypeAnsiString || type == kIntypeCountedString ||
           type == kIntypeCountedAnsiString;
}

/**
 * @brief The number of payload bytes a property of a fixed size type occupies.
 *
 * The size is what a walk of a payload advances by. A type whose size is not
 * known is what ends a walk: the property behind it cannot be found without
 * the length the event carries for it.
 *
 * @param[in] type The type of the property.
 * @param[in] pointer_size The size of a pointer of the traced process.
 * @return The size in bytes, zero when the type has no fixed size.
 */
std::size_t FixedSize(USHORT type, std::size_t pointer_size)
{
    switch (type)
    {
    case kIntypeInt8:
    case kIntypeUInt8:
        return 1;
    case kIntypeInt16:
    case kIntypeUInt16:
        return 2;
    case kIntypeInt32:
    case kIntypeUInt32:
    case kIntypeFloat:
    case kIntypeBoolean:
    case kIntypeHexInt32:
        return 4;
    case kIntypeInt64:
    case kIntypeUInt64:
    case kIntypeDouble:
    case kIntypeFileTime:
    case kIntypeHexInt64:
        return 8;
    case kIntypeGuid:
        return 16;
    case kIntypeSystemTime:
        return 16;
    case kIntypePointer:
        return pointer_size;
    default:
        return 0;
    }
}

/**
 * @brief Read a little endian value of a payload.
 * @param[in] data The value of the property.
 * @param[in] size The size of the value in bytes.
 * @return The value.
 */
std::uint64_t ReadNumber(const unsigned char* data, std::size_t size)
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < size; ++index)
    {
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8);
    }
    return value;
}

/**
 * @brief The name of a property of an event.
 * @param[in] info The metadata of the event.
 * @param[in] offset The offset of the name inside the metadata.
 * @return The name.
 */
const wchar_t* PropertyName(const TRACE_EVENT_INFO& info, ULONG offset)
{
    return reinterpret_cast<const wchar_t*>(reinterpret_cast<const unsigned char*>(&info) + offset);
}

/**
 * @brief Format a text property of an event with TDH.
 *
 * TDH reports how many bytes of the payload the text consumed, which is what
 * the walk of the payload advances by.
 *
 * @param[in] info The metadata of the event.
 * @param[in] property The property to format.
 * @param[in] pointer_size The size of a pointer of the traced process.
 * @param[in] data The payload at the property.
 * @param[in] length The number of payload bytes which are left.
 * @param[out] text The formatted text.
 * @param[out] consumed The number of bytes the text consumed.
 * @return true when the text was formatted.
 */
bool FormatText(const TRACE_EVENT_INFO& info, const EVENT_PROPERTY_INFO& property, std::size_t pointer_size,
                const unsigned char* data, std::size_t length, std::wstring& text, std::size_t& consumed)
{
    std::vector<wchar_t> buffer(256);
    for (;;)
    {
        ULONG      size = static_cast<ULONG>(buffer.size() * sizeof(wchar_t));
        USHORT     used = 0;
        const auto status =
            ::TdhFormatProperty(const_cast<PTRACE_EVENT_INFO>(&info), nullptr, static_cast<ULONG>(pointer_size),
                                property.nonStructType.InType, property.nonStructType.OutType, property.length,
                                static_cast<USHORT>(length), const_cast<PBYTE>(data), &size, buffer.data(), &used);

        if (status == ERROR_INSUFFICIENT_BUFFER && size > buffer.size() * sizeof(wchar_t))
        {
            buffer.resize(size / sizeof(wchar_t) + 1);
            continue;
        }
        if (status != ERROR_SUCCESS || used == 0)
        {
            return false;
        }

        text.assign(buffer.data());
        consumed = used;
        return true;
    }
}

/**
 * @brief The number of the payload bytes a property of an event occupies.
 *
 * The walk of a payload sizes the properties of a fixed type on its own, but a
 * property whose count, length or structure the event carries needs the values
 * of the properties which carry them, which is what the trace data helper
 * resolves. The call fails when the machine does not know the metadata of the
 * event or when the property cannot be sized at all, which is where the walk
 * stops.
 *
 * @param[in] record The event.
 * @param[in] name The name of the property.
 * @param[out] size The number of the bytes the property occupies.
 * @return true when the size was read.
 */
bool PropertySize(const EVENT_RECORD& record, const wchar_t* name, std::size_t& size)
{
    PROPERTY_DATA_DESCRIPTOR descriptor = {};
    descriptor.PropertyName = reinterpret_cast<ULONGLONG>(const_cast<wchar_t*>(name));
    descriptor.ArrayIndex = ULONG_MAX; /* The property is not an array. */

    ULONG      property_size = 0;
    const auto status =
        ::TdhGetPropertySize(const_cast<PEVENT_RECORD>(&record), 0, nullptr, 1, &descriptor, &property_size);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }

    size = property_size;
    return true;
}

} // namespace

namespace appbox
{

bool EtwEventDecoder::MetadataKey::operator==(const MetadataKey& other) const
{
    return memcmp(&provider, &other.provider, sizeof(GUID)) == 0 && id == other.id && opcode == other.opcode &&
           version == other.version;
}

std::size_t EtwEventDecoder::MetadataKeyHash::operator()(const MetadataKey& key) const
{
    /* FNV-1a over the bytes of the key, which is what keeps the hash of the
     * provider of an event apart from the hash of its identifier. */
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;

    std::uint64_t hash = kOffsetBasis;
    const auto*   bytes = reinterpret_cast<const unsigned char*>(&key);
    for (std::size_t index = 0; index < sizeof(MetadataKey); ++index)
    {
        hash ^= bytes[index];
        hash *= kPrime;
    }
    return static_cast<std::size_t>(hash);
}

bool EtwEventDecoder::IsWppEvent(const EVENT_RECORD& record)
{
    return (record.EventHeader.Flags & EVENT_HEADER_FLAG_TRACE_MESSAGE) != 0;
}

const TRACE_EVENT_INFO* EtwEventDecoder::MetadataOf(const EVENT_RECORD& record)
{
    MetadataKey key;
    key.provider = record.EventHeader.ProviderId;
    key.id = record.EventHeader.EventDescriptor.Id;
    key.opcode = record.EventHeader.EventDescriptor.Opcode;
    key.version = record.EventHeader.EventDescriptor.Version;

    const auto known = cache_.find(key);
    if (known != cache_.end())
    {
        return known->second.known ? reinterpret_cast<const TRACE_EVENT_INFO*>(known->second.bytes.data()) : nullptr;
    }

    if (cache_.size() >= kMaxMetadataEntries)
    {
        cache_.clear();
    }

    Metadata metadata;
    metadata.bytes.resize(kInitialMetadataSize);
    for (;;)
    {
        ULONG size = static_cast<ULONG>(metadata.bytes.size());
        /* TDH reads the header of the event alone and leaves the record as it
         * is, so the cast of the record is what the interface asks for. */
        const auto status = ::TdhGetEventInformation(const_cast<PEVENT_RECORD>(&record), 0, nullptr,
                                                     reinterpret_cast<PTRACE_EVENT_INFO>(metadata.bytes.data()), &size);
        if (status == ERROR_SUCCESS)
        {
            metadata.known = true;
            break;
        }
        if (status != ERROR_INSUFFICIENT_BUFFER || size <= metadata.bytes.size())
        {
            metadata.bytes.clear();
            break;
        }
        metadata.bytes.resize(size);
    }

    const auto inserted = cache_.emplace(key, std::move(metadata));
    return inserted.first->second.known ? reinterpret_cast<const TRACE_EVENT_INFO*>(inserted.first->second.bytes.data())
                                        : nullptr;
}

bool EtwEventDecoder::Read(const EVENT_RECORD& record, EtwEventFields& fields)
{
    const auto* info = MetadataOf(record);
    if (info == nullptr)
    {
        return false;
    }

    const auto pointer_size =
        (record.EventHeader.Flags & EVENT_HEADER_FLAG_32_BIT_HEADER) != 0 ? std::size_t{ 4 } : std::size_t{ 8 };
    const auto* payload = static_cast<const unsigned char*>(record.UserData);
    const auto  length = static_cast<std::size_t>(record.UserDataLength);
    std::size_t offset = 0;

    ULONG index = 0;
    for (; index < info->PropertyCount; ++index)
    {
        const auto& property = info->EventPropertyInfoArray[index];
        const auto* name = PropertyName(*info, property.NameOffset);
        const auto  remaining = offset < length ? length - offset : 0;
        if (remaining == 0)
        {
            break;
        }

        /*
         * A property whose count, length or structure the event carries cannot
         * be sized by the walk on its own, and a property of a type the walk
         * does not know has no size at all. The trace data helper resolves the
         * number of the payload bytes of such a property, which is what keeps
         * the properties behind it readable: the path of a create event is the
         * last property of its event and a version of the machine may declare a
         * structure in front of it. A property which cannot be resolved either
         * ends the walk, which the report of a recording names.
         */
        const bool sized_by_the_event =
            (property.Flags & (PropertyStruct | PropertyParamCount | PropertyParamLength)) != 0;

        if (!sized_by_the_event && IsText(property.nonStructType.InType))
        {
            std::wstring text;
            std::size_t  consumed = 0;
            if (!FormatText(*info, property, pointer_size, payload + offset, remaining, text, consumed) ||
                consumed > remaining)
            {
                break;
            }

            if (!fields.has_name && IsNamed(name, kNameNames))
            {
                fields.name = std::move(text);
                fields.has_name = true;
            }
            offset += consumed;
            continue;
        }

        const auto size = FixedSize(property.nonStructType.InType, pointer_size);
        if (sized_by_the_event || size == 0)
        {
            std::size_t resolved = 0;
            if (!PropertySize(record, name, resolved) || resolved == 0 || resolved > remaining)
            {
                break;
            }

            offset += resolved;
            continue;
        }

        if (size > remaining)
        {
            break;
        }

        if (!fields.has_object && IsNamed(name, kObjectNames))
        {
            fields.object = ReadNumber(payload + offset, size);
            fields.has_object = true;
        }
        else if (!fields.has_file_key && IsNamed(name, kFileKeyNames))
        {
            fields.file_key = ReadNumber(payload + offset, size);
            fields.has_file_key = true;
        }
        offset += size;
    }

    fields.stopped_at = index;
    fields.complete = index >= info->PropertyCount;
    return true;
}

bool EtwEventDecoder::DescribeProperties(const EVENT_RECORD& record, std::vector<EtwEventProperty>& properties)
{
    properties.clear();

    const auto* info = MetadataOf(record);
    if (info == nullptr)
    {
        return false;
    }

    properties.reserve(info->PropertyCount);
    for (ULONG index = 0; index < info->PropertyCount; ++index)
    {
        const auto& property = info->EventPropertyInfoArray[index];

        EtwEventProperty described;
        described.name = PropertyName(*info, property.NameOffset);
        described.in_type = property.nonStructType.InType;
        described.out_type = property.nonStructType.OutType;
        described.flags = static_cast<unsigned short>(property.Flags);
        described.count = property.count;
        described.length = property.length;
        properties.push_back(std::move(described));
    }

    return true;
}

bool EtwEventDecoder::DescribeEventName(const EVENT_RECORD& record, std::wstring& opcode, std::wstring& task)
{
    opcode.clear();
    task.clear();

    const auto* info = MetadataOf(record);
    if (info == nullptr)
    {
        return false;
    }

    if (info->OpcodeNameOffset != 0)
    {
        opcode = PropertyName(*info, info->OpcodeNameOffset);
    }

    if (info->TaskNameOffset != 0)
    {
        task = PropertyName(*info, info->TaskNameOffset);
    }

    return true;
}

} // namespace appbox
