#ifndef APPBOX_PACKER_CORE_ETW_EVENT_DECODER_HPP
#define APPBOX_PACKER_CORE_ETW_EVENT_DECODER_HPP

/*
 * The kernel headers have to see a version which carries the manifest
 * providers, and the definition has to come before the first windows.h of the
 * translation unit.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <evntcons.h>
#include <tdh.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace appbox
{

/**
 * @brief The fields the realtime capture reads of one event.
 *
 * The events of the capture carry the object they belong to and the name of
 * the entry the object stands for. The object is the file object of the kernel
 * for the file events and the key control block for the registry events, and
 * the name is the path of a file, the path of a registry key or the name of a
 * registry value. A file event carries a second object as well, the file key
 * of the kernel, which stays the same while a file is renamed or opened twice.
 */
struct EtwEventFields
{
    /** @brief The file object, the key object or the key handle of the event. */
    std::uint64_t object = 0;

    /** @brief The file key of an event which carries one, zero otherwise. */
    std::uint64_t file_key = 0;

    /** @brief The path or the value name the event carries. */
    std::wstring name;

    /** @brief Whether the event carried the object of the fields. */
    bool has_object = false;

    /** @brief Whether the event carried a file key. */
    bool has_file_key = false;

    /** @brief Whether the event carried a name. */
    bool has_name = false;

    /**
     * @brief The index of the property the walk of the payload stopped at.
     *
     * The walk stops where the payload carries a property whose size the event
     * itself names and which TDH cannot size either, so the fields behind it
     * are not read. The index names that property in the report of a
     * recording, which is what tells a reader why an event was left out.
     */
    unsigned stopped_at = 0;

    /** @brief Whether the walk read every property of the event. */
    bool complete = false;
};

/**
 * @brief One property of the metadata of an event.
 *
 * The properties are the ones the manifest of the provider declares for the
 * event, in the order the payload carries them. The report of a recording names
 * them, which is what tells a reader why an event could not be read: the walk
 * of a payload stops where a property carries a structure, an array or a length
 * which the event itself names, and the name of that property is the one a
 * change of the manifest is recognized by.
 */
struct EtwEventProperty
{
    /** @brief The name of the property. */
    std::wstring name;

    /** @brief The type of the property inside the payload. */
    unsigned short in_type = 0;

    /** @brief The type the property is reported as. */
    unsigned short out_type = 0;

    /** @brief The flags of the property, see the `PROPERTY_FLAGS` of TDH. */
    unsigned short flags = 0;

    /**
     * @brief The number of the elements of the property.
     *
     * The value is the index of the property which carries the number when the
     * property is flagged with `PropertyParamCount`.
     */
    unsigned short count = 0;

    /**
     * @brief The length of one element of the property, in bytes.
     *
     * The value is the index of the property which carries the length when the
     * property is flagged with `PropertyParamLength`.
     */
    unsigned short length = 0;
};

/**
 * @brief Reads the properties of an ETW event by their name.
 *
 * The payload of an event is decoded with the trace data helper of Windows
 * (TDH): the metadata of an event is asked for once per provider, event
 * identifier and version and is kept, and the properties which follow are read
 * by their name. The layout of an event belongs to the manifest of its
 * provider and differs between the versions of Windows, so an offset into a
 * payload cannot be written down while a name can.
 *
 * The metadata is read for the events of the providers the realtime capture
 * subscribes to, so the cache holds a handful of entries; it is bounded all
 * the same, because a decoder which is fed with the events of other providers
 * would otherwise grow without a limit. A decoder belongs to the consumer
 * thread of a recording and is not shared, so its cache needs no lock.
 *
 * The names of the properties are the ones the manifests of the two providers
 * declare:
 *
 * * The object is `FileObject` of the file events of the kernel and
 *   `KeyObject` or `KeyHandle` of its registry events.
 * * The file key is `FileKey`.
 * * The name is `FileName` or `FilePath` of a file event and `KeyName` of a
 *   registry event; the registry events which carry a value name it in their
 *   `KeyName` property.
 */
class EtwEventDecoder
{
public:
    /**
     * @brief Create the decoder with an empty metadata cache.
     */
    EtwEventDecoder() = default;

    EtwEventDecoder(const EtwEventDecoder&) = delete;
    EtwEventDecoder& operator=(const EtwEventDecoder&) = delete;

    /**
     * @brief Whether an event is a WPP message rather than an event of a provider.
     *
     * A WPP message is written by a driver which was built with the software
     * trace preprocessor, and its payload carries a trace message rather than
     * the properties of a manifest. The capture records the events of the
     * kernel alone, so such an event is recognized and left alone instead of
     * being decoded.
     *
     * @param[in] record The event to check.
     * @return true when the event is a WPP message.
     */
    static bool IsWppEvent(const EVENT_RECORD& record);

    /**
     * @brief Read the fields of one event.
     *
     * The payload of the event is walked once: every property of the fields is
     * looked up by its name while the walk passes it, so the cost of an event
     * is the cost of one walk and not of one walk per property. A property the
     * event does not carry leaves the matching field at its default value, and
     * the walk stops where the layout of the event stops being a list of
     * values of a known size.
     *
     * @param[in] record The event to read.
     * @param[out] fields The fields of the event.
     * @return true when the metadata of the event was read; false when the
     *         machine does not know the provider of the event.
     */
    bool Read(const EVENT_RECORD& record, EtwEventFields& fields);

    /**
     * @brief Describe the properties of the metadata of one event.
     *
     * The description is what the report of a recording names for the events it
     * could not resolve: the properties of the event, their types and the flags
     * which decide whether the walk of the payload can size them.
     *
     * @param[in] record The event to describe.
     * @param[out] properties The properties of the event, empty when the
     *             machine does not know the provider of the event.
     * @return true when the metadata of the event was read.
     */
    bool DescribeProperties(const EVENT_RECORD& record, std::vector<EtwEventProperty>& properties);

    /**
     * @brief Describe the names a machine gives one event.
     *
     * The names belong to the metadata of the machine: the manifest of a
     * provider names the task of an event and its opcode, and the metadata of a
     * classic provider names the event type of the event in one of the two. The
     * capture resolves the events it reads by those names, because the
     * identifier of an event belongs to the version of the manifest of the
     * machine and differs between the versions of Windows.
     *
     * @param[in] record The event to describe.
     * @param[out] opcode The name of the opcode of the event, empty when the
     *             machine gives it none.
     * @param[out] task The name of the task of the event, empty when the
     *             machine gives it none.
     * @return true when the metadata of the event was read.
     */
    bool DescribeEventName(const EVENT_RECORD& record, std::wstring& opcode, std::wstring& task);

private:
    /**
     * @brief The identifier of the metadata of one event.
     *
     * The metadata of an event is the one of its provider, its event
     * identifier, its event type and its version: two events of one provider
     * which share the identifier but not the version carry different
     * properties, and the event type of a classic event is the one its opcode
     * names while its identifier is empty.
     */
    struct MetadataKey
    {
        /** @brief The provider of the event. */
        GUID provider = {};

        /** @brief The event identifier of the event. */
        unsigned short id = 0;

        /** @brief The event type of the event. */
        unsigned char opcode = 0;

        /** @brief The version of the event. */
        unsigned char version = 0;

        /**
         * @brief Whether two keys describe the same metadata.
         * @param[in] other The key to compare with.
         * @return true when the keys are equal.
         */
        bool operator==(const MetadataKey& other) const;
    };

    /**
     * @brief The hash of a metadata key.
     */
    struct MetadataKeyHash
    {
        /**
         * @brief The hash of a key.
         * @param[in] key The key to hash.
         * @return The hash value.
         */
        std::size_t operator()(const MetadataKey& key) const;
    };

    /**
     * @brief The metadata of one event.
     */
    struct Metadata
    {
        /** @brief The buffer TDH filled, empty when the metadata is unknown. */
        std::vector<std::uint8_t> bytes;

        /** @brief Whether the buffer holds the metadata of the event. */
        bool known = false;
    };

    /**
     * @brief The metadata of an event, from the cache or from TDH.
     * @param[in] record The event.
     * @return The metadata of the event, null when the machine does not know
     *         the provider of the event.
     */
    const TRACE_EVENT_INFO* MetadataOf(const EVENT_RECORD& record);

    /**
     * @brief The metadata of the events which were read, keyed by provider,
     *        event identifier and version.
     */
    std::unordered_map<MetadataKey, Metadata, MetadataKeyHash> cache_;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_ETW_EVENT_DECODER_HPP
