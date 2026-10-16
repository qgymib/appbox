#include <gtest/gtest.h>
#include "src/core/EtwEventDecoder.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace
{

/** The manifest provider of the file events of the kernel. */
constexpr GUID kKernelFileGuid = {
    0xEDD08927, 0x9CC4, 0x4E65, { 0xB9, 0x70, 0xC2, 0x56, 0x0F, 0xB5, 0xC2, 0x89 }
};

/** The manifest provider of the registry events of the kernel. */
constexpr GUID kKernelRegistryGuid = {
    0x70EB4F03, 0xC1DE, 0x4F73, { 0xA0, 0x51, 0x33, 0xD1, 0x3D, 0x54, 0x13, 0xBD }
};

/** The classic provider of the file events of the kernel. */
constexpr GUID kFileIoGuid = {
    0x90CBDC39, 0x4A3E, 0x11D1, { 0x84, 0xF4, 0x00, 0x00, 0xF8, 0x04, 0x64, 0xE3 }
};

/** The classic provider of the registry events of the kernel. */
constexpr GUID kRegistryGuid = {
    0xAE53722E, 0xC863, 0x11D2, { 0x86, 0x59, 0x00, 0xC0, 0x4F, 0xA3, 0x21, 0xA1 }
};

/** A provider no machine knows. */
constexpr GUID kUnknownGuid = {
    0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77 }
};

/** The identifier of the create event of the file provider. */
constexpr unsigned short kFileEventCreate = 12;

/** The identifier of the create new file event of the file provider. */
constexpr unsigned short kFileEventCreateNewFile = 30;

/** The identifier of the write event of the file provider. */
constexpr unsigned short kFileEventWrite = 16;

/** The identifier of the set value event of the registry provider. */
constexpr unsigned short kRegistryEventSetValue = 5;

/** The event type of the set value event of the classic registry provider. */
constexpr unsigned short kRegistryOpSetValue = 14;

/** The event type of the delete path event of the classic file provider. */
constexpr unsigned short kFileOpDeletePath = 79;

/**
 * @brief Builds the payload of a synthetic event.
 *
 * The values are appended in the order the manifest of the provider declares
 * them, so a case describes the layout of an event version the way the payload
 * of the event carries it.
 */
class PayloadBuilder
{
public:
    /**
     * @brief Append a pointer of the traced process.
     * @param[in] value The value of the pointer.
     * @return The builder.
     */
    PayloadBuilder& Pointer(std::uint64_t value)
    {
        return Number(value, 8);
    }

    /**
     * @brief Append a 32 bit value.
     * @param[in] value The value.
     * @return The builder.
     */
    PayloadBuilder& UInt32(std::uint32_t value)
    {
        return Number(value, 4);
    }

    /**
     * @brief Append a 64 bit value.
     * @param[in] value The value.
     * @return The builder.
     */
    PayloadBuilder& UInt64(std::uint64_t value)
    {
        return Number(value, 8);
    }

    /**
     * @brief Append a null terminated text.
     * @param[in] text The text.
     * @return The builder.
     */
    PayloadBuilder& Text(const std::wstring& text)
    {
        for (const auto character : text)
        {
            bytes_.push_back(static_cast<unsigned char>(character & 0xFF));
            bytes_.push_back(static_cast<unsigned char>((character >> 8) & 0xFF));
        }
        bytes_.push_back(0);
        bytes_.push_back(0);
        return *this;
    }

    /**
     * @brief The payload which was built.
     * @return The bytes of the payload.
     */
    const std::vector<unsigned char>& Bytes() const
    {
        return bytes_;
    }

private:
    /**
     * @brief Append a little endian number.
     * @param[in] value The value.
     * @param[in] size The size of the number in bytes.
     * @return The builder.
     */
    PayloadBuilder& Number(std::uint64_t value, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            bytes_.push_back(static_cast<unsigned char>((value >> (index * 8)) & 0xFF));
        }
        return *this;
    }

    /** The bytes of the payload. */
    std::vector<unsigned char> bytes_;
};

/**
 * @brief One synthetic event which is handed to the decoder.
 *
 * The payload is owned by the case and the record points into it, so a case
 * keeps the object alive while it reads the event.
 */
class SyntheticEvent
{
public:
    /**
     * @brief Build the event of a payload.
     * @param[in] provider The provider of the event.
     * @param[in] id The event identifier of the event.
     * @param[in] version The version of the event.
     * @param[in] payload The bytes of the payload.
     * @param[in] classic Whether the event is a classic event, which carries
     *            its event type as its opcode and no event identifier.
     */
    SyntheticEvent(const GUID& provider, unsigned short id, unsigned char version,
                   const std::vector<unsigned char>& payload, bool classic = false)
        : payload_(payload)
    {
        record_.EventHeader.ProviderId = provider;
        record_.EventHeader.EventDescriptor.Id = classic ? 0 : id;
        record_.EventHeader.EventDescriptor.Opcode = classic ? static_cast<unsigned char>(id) : 0;
        record_.EventHeader.EventDescriptor.Version = version;
        record_.EventHeader.Flags = EVENT_HEADER_FLAG_64_BIT_HEADER;
        if (classic)
        {
            /* A classic event is the one whose header says so, and its event
             * type is the one its opcode carries. */
            record_.EventHeader.Flags |= EVENT_HEADER_FLAG_CLASSIC_HEADER;
        }
        record_.UserDataLength = static_cast<unsigned short>(payload_.size());
        record_.UserData = payload_.empty() ? nullptr : const_cast<unsigned char*>(payload_.data());
    }

    /**
     * @brief The event which is handed to the decoder.
     * @return The record of the event.
     */
    const EVENT_RECORD& Record() const
    {
        return record_;
    }

    /**
     * @brief Mark the event as a WPP message.
     */
    void MarkAsWpp()
    {
        record_.EventHeader.Flags |= EVENT_HEADER_FLAG_TRACE_MESSAGE;
    }

private:
    /** The payload of the event. */
    std::vector<unsigned char> payload_;

    /** The record which points into the payload. */
    EVENT_RECORD record_{};
};

/**
 * @brief Read the fields of a synthetic event.
 * @param[in] event The event to read.
 * @param[out] fields The fields of the event.
 * @return true when the metadata of the event was read.
 */
bool ReadFields(const SyntheticEvent& event, appbox::EtwEventFields& fields)
{
    appbox::EtwEventDecoder decoder;
    return decoder.Read(event.Record(), fields);
}

} // namespace

/**
 * The create event of the file provider is declared in two versions which lay
 * the same values out differently: the version zero carries the thread
 * identifier as a pointer before the file object and the version one carries
 * it as a 32 bit value behind the file object.
 *
 * Expected: both versions yield the same file object and the same path, which
 * is what a decoder which reads the properties by their name gives and what an
 * offset into the payload cannot give.
 */
TEST(Unit_EtwEventDecoder, ReadsTheSameEventOfTwoLayouts)
{
    const std::wstring path = L"\\Device\\HarddiskVolume3\\Users\\Vendor\\App.exe";

    PayloadBuilder first;
    first.Pointer(0x1000).Pointer(0x2000).Pointer(0x3000).UInt32(0x40).UInt32(0x80).UInt32(0x7).Text(path);
    const SyntheticEvent version_zero(kKernelFileGuid, kFileEventCreate, 0, first.Bytes());

    PayloadBuilder second;
    second.Pointer(0x1000).Pointer(0x3000).UInt32(0x2000).UInt32(0x40).UInt32(0x80).UInt32(0x7).Text(path);
    const SyntheticEvent version_one(kKernelFileGuid, kFileEventCreate, 1, second.Bytes());

    appbox::EtwEventFields zero;
    appbox::EtwEventFields one;
    ASSERT_TRUE(ReadFields(version_zero, zero));
    ASSERT_TRUE(ReadFields(version_one, one));

    EXPECT_TRUE(zero.has_object);
    EXPECT_TRUE(one.has_object);
    EXPECT_EQ(zero.object, 0x3000u);
    EXPECT_EQ(one.object, 0x3000u);
    EXPECT_TRUE(zero.has_name);
    EXPECT_TRUE(one.has_name);
    EXPECT_EQ(zero.name, path);
    EXPECT_EQ(one.name, path);
}

/**
 * The classic delete path event of the file provider carries its path at the
 * end of a payload which is a list of pointers and integers in front of it,
 * and its event type is the one its opcode names.
 *
 * Expected: the file object of the event is the one the property carries and
 * not the first pointer of the payload, and the path is read.
 */
TEST(Unit_EtwEventDecoder, ReadsTheClassicDeletePathEvent)
{
    const std::wstring path = L"\\Device\\HarddiskVolume3\\Users\\Vendor\\Install\\App.exe";

    PayloadBuilder payload;
    payload.Pointer(0x1111).Pointer(0x2222).Pointer(0x3333).Pointer(0x4444).UInt32(0x5555).UInt32(0x6).Text(path);
    const SyntheticEvent event(kFileIoGuid, kFileOpDeletePath, 3, payload.Bytes(), true);

    appbox::EtwEventFields fields;
    ASSERT_TRUE(ReadFields(event, fields));
    EXPECT_TRUE(fields.has_object);
    EXPECT_EQ(fields.object, 0x2222u);
    EXPECT_TRUE(fields.has_file_key);
    EXPECT_EQ(fields.file_key, 0x3333u);
    EXPECT_TRUE(fields.has_name);
    EXPECT_EQ(fields.name, path);
}

/**
 * The write event of the file provider carries the file object and the file
 * key, which is what ties a write to the create which named the file.
 *
 * Expected: both objects are read.
 */
TEST(Unit_EtwEventDecoder, ReadsTheFileObjectAndTheFileKeyOfAWrite)
{
    PayloadBuilder payload;
    payload.UInt64(0x1000).Pointer(0x2000).Pointer(0x3000).Pointer(0x4000).UInt32(0x5000).UInt32(0x20).UInt32(0x0);
    const SyntheticEvent event(kKernelFileGuid, kFileEventWrite, 1, payload.Bytes());

    appbox::EtwEventFields fields;
    ASSERT_TRUE(ReadFields(event, fields));
    EXPECT_TRUE(fields.has_object);
    EXPECT_EQ(fields.object, 0x3000u);
    EXPECT_TRUE(fields.has_file_key);
    EXPECT_EQ(fields.file_key, 0x4000u);
    EXPECT_FALSE(fields.has_name);
}

/**
 * The create new file event of the file provider is the signal of an entry
 * which came into being, so the decoder has to reach the path of it.
 *
 * Expected: the path of the event is read.
 */
TEST(Unit_EtwEventDecoder, ReadsThePathOfTheCreateNewFileEvent)
{
    const std::wstring path = L"\\Device\\HarddiskVolume3\\Users\\Vendor\\Install\\App.exe";

    PayloadBuilder payload;
    payload.Pointer(0x1000).Pointer(0x2000).UInt32(0x3000).UInt32(0x40).UInt32(0x80).UInt32(0x3).Text(path);
    const SyntheticEvent event(kKernelFileGuid, kFileEventCreateNewFile, 1, payload.Bytes());

    appbox::EtwEventFields fields;
    ASSERT_TRUE(ReadFields(event, fields));
    EXPECT_EQ(fields.object, 0x2000u);
    EXPECT_EQ(fields.name, path);
}

/**
 * The set value event of the classic registry provider carries the key control
 * block and the name of the value, which the walk has to find behind the
 * values in front of them.
 *
 * Expected: the key handle and the value name are read.
 */
TEST(Unit_EtwEventDecoder, ReadsTheKeyHandleAndTheValueNameOfTheClassicRegistryEvent)
{
    PayloadBuilder payload;
    payload.UInt64(0x1000).UInt32(0x0).UInt32(0x0).Pointer(0x2000).Text(L"InstallDir");
    const SyntheticEvent event(kRegistryGuid, kRegistryOpSetValue, 2, payload.Bytes(), true);

    appbox::EtwEventFields fields;
    ASSERT_TRUE(ReadFields(event, fields));
    EXPECT_TRUE(fields.has_object);
    EXPECT_EQ(fields.object, 0x2000u);
    EXPECT_TRUE(fields.has_name);
    EXPECT_EQ(fields.name, L"InstallDir");
}

/**
 * The set value event of the manifest registry provider carries a binary whose
 * length the event names, which a walk of the payload cannot size on its own.
 *
 * Expected: the walk resolves the length of the binary with the trace data
 * helper and reads the properties behind it as well, so a property of a
 * variable size does not hide the rest of the event.
 */
TEST(Unit_EtwEventDecoder, WalksPastAPropertyWhoseLengthTheEventCarries)
{
    PayloadBuilder payload;
    payload.Pointer(0x1000).UInt32(0x0).UInt32(0x1).UInt32(0xC).Text(L"Software\\Vendor").Text(L"InstallDir");
    payload.UInt32(0xC).Text(L"binary");
    const SyntheticEvent event(kKernelRegistryGuid, kRegistryEventSetValue, 0, payload.Bytes());

    appbox::EtwEventDecoder               decoder;
    std::vector<appbox::EtwEventProperty> properties;
    ASSERT_TRUE(decoder.DescribeProperties(event.Record(), properties));

    /* The property the walk cannot size on its own is the one whose count,
     * length or structure the event carries. */
    std::size_t variable = properties.size();
    for (std::size_t index = 0; index < properties.size(); ++index)
    {
        if ((properties[index].flags & (PropertyStruct | PropertyParamCount | PropertyParamLength)) != 0)
        {
            variable = index;
            break;
        }
    }
    ASSERT_LT(variable, properties.size()) << "the event carries no property of a variable size";

    appbox::EtwEventFields fields;
    ASSERT_TRUE(decoder.Read(event.Record(), fields));
    EXPECT_TRUE(fields.has_object);
    EXPECT_EQ(fields.object, 0x1000u);
    EXPECT_TRUE(fields.has_name);
    EXPECT_EQ(fields.name, L"Software\\Vendor");
    EXPECT_GT(fields.stopped_at, variable) << "the walk stopped at the property " << fields.stopped_at;
}

/**
 * The walk of a payload ends where a property cannot be sized at all: a payload
 * which is shorter than the properties of its event names.
 *
 * Expected: the walk stops at the property the payload does not hold and
 * reports where it stopped, which is what the report of a recording names.
 */
TEST(Unit_EtwEventDecoder, ReportsWhereTheWalkOfAShortPayloadStopped)
{
    PayloadBuilder payload;
    payload.Pointer(0x1000).Pointer(0x2000);
    const SyntheticEvent event(kKernelFileGuid, kFileEventCreate, 0, payload.Bytes());

    appbox::EtwEventFields fields;
    ASSERT_TRUE(ReadFields(event, fields));
    EXPECT_FALSE(fields.complete);
    EXPECT_GT(fields.stopped_at, 0u);
}

/**
 * A provider no machine knows carries no metadata, so its events cannot be
 * decoded.
 *
 * Expected: the read is reported as failed and the fields stay empty.
 */
TEST(Unit_EtwEventDecoder, ReportsAnEventOfAnUnknownProvider)
{
    PayloadBuilder payload;
    payload.Pointer(0x1000).Text(L"a path");
    const SyntheticEvent event(kUnknownGuid, kFileEventCreate, 0, payload.Bytes());

    appbox::EtwEventFields fields;
    EXPECT_FALSE(ReadFields(event, fields));
    EXPECT_FALSE(fields.has_object);
    EXPECT_FALSE(fields.has_name);
}

/**
 * A WPP message carries a trace message rather than the properties of a
 * manifest, so the capture leaves it alone.
 *
 * Expected: the event is reported as a WPP message, and an event of a manifest
 * is not.
 */
TEST(Unit_EtwEventDecoder, RecognizesAWppMessage)
{
    PayloadBuilder payload;
    payload.Pointer(0x1000);
    SyntheticEvent event(kKernelFileGuid, kFileEventCreate, 0, payload.Bytes());

    EXPECT_FALSE(appbox::EtwEventDecoder::IsWppEvent(event.Record()));

    event.MarkAsWpp();
    EXPECT_TRUE(appbox::EtwEventDecoder::IsWppEvent(event.Record()));
}
