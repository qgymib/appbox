#ifndef APPBOX_COMMON_ERROR_CODE_HPP
#define APPBOX_COMMON_ERROR_CODE_HPP

/*
 * The single table of the failures an interface of `common/` reports.
 *
 * A failure is described by its code alone: the header defines no text, no
 * `...Message()` function and includes no logging header, because the module
 * which shows a failure owns the wording of it.
 *
 * The values are grouped in ranges by domain, so a later stage adds the codes
 * of its domain without renumbering the codes which exist already:
 *
 * - `1` to `99`: general failures;
 * - `100` to `199`: text;
 * - `200` to `299`: filesystem and path;
 * - `300` to `399`: registry, network, environment and the isolation documents
 *   of the four domains;
 * - `400` to `499`: embedded resources, zip archives, PE images and the RPC
 *   layer.
 *
 * The codes of one range are appended from the start of it, so a range is
 * full once the value of its last code is the last value of the range.
 *
 * The value `0` is `kSuccess` and describes the absence of a failure. It is
 * not a failure of the table: an interface which reports its outcome as an
 * `ErrorCode` answers `kSuccess` for the normal path, while an interface which
 * reports a value or a failure answers an `expected` and never produces it.
 * The success value carries no row of `kWin32ErrorMappings`, which is the same
 * rule the failed codes no Win32 call produces follow.
 */

#include <windows.h>
#include <cstdint>

namespace appbox
{

/**
 * @brief Failure an interface of `common/` reports.
 *
 * A code names the class of a failure and not the place which detected it: the
 * same code is reused wherever the same failure is meant, and a new code is
 * added only when a caller has to tell two failures apart.
 */
enum class ErrorCode : std::int32_t
{
    /* The absence of a failure, reported by an interface which answers a code. */

    /** The operation completed. No failure happened. */
    kSuccess = 0,

    /* General failures, range 1 to 99. */

    /** A value the caller passed does not meet the contract of the function. */
    kInvalidArgument = 1,

    /** A pointer the caller passed is null where a value is required. */
    kNullPointer = 2,

    /** A buffer the caller passed cannot hold the answer. */
    kBufferTooSmall = 3,

    /** The object a caller asked for does not exist. */
    kNotFound = 4,

    /** The object a caller asked to create exists already. */
    kAlreadyExists = 5,

    /** The caller is not allowed to reach the object. */
    kAccessDenied = 6,

    /** The answer cannot be allocated. */
    kOutOfMemory = 7,

    /** The request is well formed and this build does not implement it. */
    kNotSupported = 8,

    /** The object is not in a state which allows the request. */
    kInvalidState = 9,

    /** The request did not complete in the time the caller granted. */
    kTimeout = 10,

    /** The request was abandoned, by the caller or by the end of the object. */
    kCancelled = 11,

    /** The transport of a read or a write failed. */
    kIoFailure = 12,

    /* Text, range 100 to 199. */

    /** A text conversion was asked for a null pointer. */
    kTextInputNull = 100,

    /** A text cannot be converted between the code pages. */
    kTextConversionFailed = 101,

    /**
     * A text does not start with the prefix a caller compared it against.
     *
     * The code names the outcome of a comparison carried out on a text and not
     * a failure of a call: a caller which uses the comparison as an ordinary
     * branch reads it as "the prefixes differ", while a caller which edits the
     * prefix of a path knows that the text is not the shape it expected.
     */
    kPrefixMismatch = 102,

    /* Filesystem and path, range 200 to 299. */

    /**
     * A path of the view is covered by no entry of the isolation table.
     *
     * The caller applies the default of the view to such a path; the code
     * exists so that "the table says nothing about this path" is told apart
     * from a lookup which produced an answer.
     */
    kIsolationEntryNotListed = 200,

    /**
     * The layer key of a virtual path names no layer of the view.
     *
     * A path of the isolation document whose first component is not the key of
     * any layer cannot be expressed as a path of the view.
     */
    kLayerKeyUnmapped = 201,

    /** A path helper was asked for a part of a text which carries no path. */
    kPathEmpty = 202,

    /**
     * A path names no alternate data stream.
     *
     * The separator between the entry and the name of its stream is what a
     * caller asks this code for: the answer is a position of the text, and a
     * text without a stream has none. The code says so instead of answering a
     * position which stands for "not found".
     */
    kPathNoStreamName = 203,

    /**
     * A path belongs to a namespace which names no entry of the view.
     *
     * A share of a network, a named pipe, a mailslot and a device are other
     * isolation domains of the sandbox: the view holds no entry behind such a
     * name, so the mapping refuses it instead of guessing a path of a layer.
     */
    kPathNotInViewNamespace = 204,

    /**
     * The drive of a path is no local file system.
     *
     * A network drive, a drive of an unknown type and a drive which exists but
     * cannot be reached all describe entries of another machine or of no
     * machine, which the view does not hold.
     */
    kPathNotLocalDrive = 205,

    /**
     * The volume of a path is mounted in no drive of the machine.
     *
     * A volume GUID path names a volume which may be mounted in a directory
     * only, which gives the volume no letter and therefore no path of the view.
     */
    kPathVolumeUnmounted = 206,

    /**
     * The device of a path belongs to no local drive.
     *
     * A device path names the object behind a handle, and the device map of
     * the system is what tells which drive holds it. A device the map does not
     * know is reported apart from a path which is no device path at all.
     */
    kPathDeviceUnknown = 207,

    /**
     * A path is no absolute path of a drive.
     *
     * The mappings of the view understand the two object namespace forms which
     * open a drive, `\??\X:\...` and `\GLOBAL??\X:\...`, and nothing else: a
     * path of another shape names no entry the view could key by a drive.
     */
    kPathNotDosDrivePath = 208,

    /**
     * The components of a path climb above the root of its drive.
     *
     * The parent references of a path are collapsed before the path is mapped,
     * and a reference which would leave the drive names no entry of the view.
     */
    kPathEscapesDriveRoot = 209,

    /** A text is no layer key of the table of the known folders. */
    kKnownFolderUnknown = 210,

    /**
     * A known folder cannot be resolved on this machine.
     *
     * The folder exists in the table, yet the API which reports its path
     * refuses: a folder of a machine which is not joined to a domain, a folder
     * of another user which the current token does not reach, or a folder this
     * version of the operating system does not carry.
     */
    kKnownFolderUnavailable = 211,

    /**
     * The data of a reparse point is no record the view understands.
     *
     * The layout is what the file system stores, so a record which is shorter
     * than the name it announces and a tag which redirects no namespace both
     * land here: the view can neither follow the record nor keep it.
     */
    kReparseDataInvalid = 212,

    /**
     * The target of a reparse point is no path of the view.
     *
     * The view resolves a link itself, so the link may only point at an entry
     * the view can key by a drive: a target of another namespace is refused
     * instead of forwarded to the file system of a layer.
     */
    kReparseTargetNotInView = 213,

    /**
     * A chain of reparse points is deeper than the view follows.
     *
     * A link may name another link, which names a third one. The view follows
     * the chain up to a fixed depth, which is the same kind of limit the file
     * system applies, and reports the limit instead of following it forever.
     */
    kReparseDepthExceeded = 214,

    /* Registry, network and environment, range 300 to 399. */

    /** A text does not name an isolation mode of the domain which read it. */
    kIsolationTokenUnknown = 300,

    /** A text does not name a filesystem entry kind. */
    kEntryKindTokenUnknown = 301,

    /**
     * The text of an isolation document does not fit the schema of its domain.
     *
     * The code covers every refusal of a document: a text which is no JSON
     * document, a member which is missing or of another type, and a value the
     * schema does not allow.
     */
    kIsolationDocumentInvalid = 302,

    /** The document was written with a schema version this build does not know. */
    kIsolationDocumentUnsupportedVersion = 303,

    /** The text of a port is empty. */
    kPortEmpty = 304,

    /** The text of a port holds a character which is not a decimal digit. */
    kPortNotDecimal = 305,

    /** The text of a port carries more than one digit and starts with a zero. */
    kPortLeadingZero = 306,

    /** The text of a port is not between 1 and 65535. */
    kPortOutOfRange = 307,

    /** A text is not an IPv4 or an IPv6 address literal of the accepted syntax. */
    kAddressMalformed = 308,

    /* Embedded resources, payload trailer and zip archives, range 400 to 499. */

    /**
     * The module carries no resource of the requested identifier.
     *
     * The lookup of the resource failed, so the executable was not written with
     * the payload the caller expects: a launcher which was not packed carries no
     * configuration resource, and a packer which was built without the launcher
     * payload carries none either.
     */
    kEmbeddedResourceMissing = 400,

    /**
     * The resource of the requested identifier holds no byte.
     *
     * The resource exists, yet a payload of length zero is no payload: the
     * caller cannot tell an empty resource from one which was not found, which
     * is the reason the two failures carry a code each.
     */
    kEmbeddedResourceEmpty = 401,

    /**
     * The resource of the requested identifier cannot be locked.
     *
     * The resource was found and it carries bytes, yet the image cannot hand
     * them out. The payload of the running executable is read through the view
     * of its own image, so a failure here means the image is unavailable.
     */
    kEmbeddedResourceLockFailed = 402,

    /**
     * The program file cannot be measured, opened or read.
     *
     * The trailer of a self-extracting program lives in its last block, so the
     * caller has to read the file to know whether the program carries one. A
     * file which cannot be read is reported apart from a file which carries no
     * trailer, because the first is a damaged installation and the second is an
     * ordinary program.
     */
    kPayloadTrailerUnreadable = 403,

    /**
     * The program file ends with a trailer whose fields do not hold.
     *
     * The magic of the trailer is there, so the program claims to carry a
     * payload, yet the fields describe no region in front of the trailer. A
     * damaged program is reported here instead of being read as an ordinary
     * one, which would start the run without its payload.
     */
    kPayloadTrailerInconsistent = 404,

    /**
     * An archive cannot be opened.
     *
     * The path names no file, a region of another file holds no archive, or the
     * archive is damaged. The library reports the reason, which the caller
     * cannot act on: the extraction has nothing to read either way.
     */
    kZipOpenFailed = 405,

    /** The destination folder of an extraction cannot be created. */
    kZipCreateDestinationFailed = 406,

    /** An entry of an archive cannot be opened for reading. */
    kZipOpenEntryFailed = 407,

    /** The content of an entry cannot be read out of the archive. */
    kZipReadEntryFailed = 408,

    /** The name of an entry cannot be decoded into a path. */
    kZipEntryNameUnreadable = 409,

    /**
     * The name of an entry would escape the destination folder.
     *
     * An absolute path, a drive relative path and a parent reference are the
     * three forms which name an entry outside the folder the archive is
     * extracted into, so the whole extraction is refused rather than writing
     * one file of it elsewhere.
     */
    kZipEntryNameUnsafe = 410,

    /** A file or a folder of an archive cannot be created on disk. */
    kZipCreateEntryFailed = 411,

    /** The content of an entry cannot be written to disk. */
    kZipWriteEntryFailed = 412,

    /**
     * The progress callback of an extraction aborted the run.
     *
     * The caller owns the callback and it returns false to stop, so the code
     * names a decision of the caller and not a failure of the extraction.
     */
    kZipCancelled = 413,

    /* PE images, range 400 to 499, continued from the zip range. */

    /**
     * A program file cannot be opened or is empty.
     *
     * The parser measures, opens and reads the file itself, so a missing file,
     * a file the caller may not read and an empty file all land here: the
     * parser has nothing to look at in each of them.
     */
    kPeFileUnreadable = 414,

    /**
     * A read inside an image runs past the bytes the buffer holds.
     *
     * An image which was cut short, or whose header claims a table beyond the
     * end of the file, is reported here: the position is valid for the field
     * which names it and there are no bytes to read it from.
     */
    kPeImageTruncated = 415,

    /**
     * A buffer is no PE image.
     *
     * The signature at the offset the DOS header names is not `PE\0\0`, or the
     * optional header carries a magic which is neither PE32 nor PE32+. The
     * buffer holds a file, and the file is something else.
     */
    kPeNotAnImage = 416,

    /**
     * An RVA belongs to no section of the image.
     *
     * The section table is what turns an RVA into an offset of the file, so a
     * position outside every section is a section table which does not cover
     * the image, or a header which points outside what it declares.
     */
    kPeInvalidSection = 417,

    /**
     * The export directory of an image does not hold.
     *
     * The export directory was found, yet its fields contradict each other:
     * it announces names without naming the arrays which hold them, or one of
     * its arrays lies outside the image. A caller cannot use any entry of such
     * a directory, which is why the whole directory is refused.
     */
    kPeInvalidExportDirectory = 418,

    /**
     * A forwarder string is no `module.function` form.
     *
     * A forwarded export names the module and the function which implement it
     * in one string. A string without a separator, with an empty module or
     * with an empty function describes no target, so the tracer cannot follow
     * the export.
     */
    kForwarderMalformed = 419,

    /* RPC layer, range 420 to 499, continued from the PE image range. */

    /**
     * A frame header carries the wrong magic.
     *
     * The magic is what tells a frame of this protocol from the bytes of
     * another one, so a header which does not carry it means the two ends do
     * not speak the same protocol. The stream cannot be resynchronised, which
     * is why the session drops the connection.
     */
    kRpcFrameMagicInvalid = 420,

    /**
     * The length field of a frame header names a payload above the limit.
     *
     * The length is a value the peer chose, so it is checked before anything
     * is allocated: a corrupted or a hostile length would otherwise ask for a
     * buffer this process cannot hold.
     */
    kRpcFrameTooLarge = 421,

    /** An RPC response is not a JSON object. */
    kRpcResponseNotObject = 422,

    /** An RPC response carries no `id`, or an `id` which is no number. */
    kRpcResponseIdMissing = 423,

    /** An RPC response carries neither a `result` nor an `error` member. */
    kRpcResponseEmpty = 424,

    /**
     * An RPC response carries a member this protocol cannot read.
     *
     * The `id` is a number outside the range of a request id, or the `error`
     * member is present and holds no error object. The answer names no request
     * this session could deliver it to.
     */
    kRpcResponseInvalid = 425,

    /** An event of a session cannot be created. */
    kRemoteEventCreateFailed = 426,

    /** The thread which serves a session cannot be started. */
    kRemoteThreadStartFailed = 427,

    /** A pipe of the name already has a server of this user. */
    kServerAlreadyRunning = 428,

    /** The name of the pipe is taken and cannot be reached by this user. */
    kServerAccessDenied = 429,

    /** The instance of the pipe cannot be created. */
    kServerCreateFailed = 430,

    /** The pipe of the name was not served within the budget of the connect. */
    kPipeConnectTimeout = 431,

    /** The pipe of the name cannot be opened. */
    kPipeConnectFailed = 432,

    /** The mode of the connected pipe cannot be set. */
    kPipeStateFailed = 433,
};

/**
 * @brief One row of the mapping between a Win32 code and a code of the table.
 *
 * The mapping is many to one: several Win32 codes describe the same failure of
 * this table, which is the reason a code is reused instead of growing one entry
 * per Win32 error. The rows of one code are adjacent, and the first row of a
 * code decides what `Win32FromErrorCode()` answers, so the reverse mapping is
 * defined for every code: it names one Win32 code which produces the code, or
 * `ERROR_SUCCESS` for a code no Win32 code produces.
 */
struct Win32ErrorMapping
{
    ErrorCode code;  /* Code of the table. */
    DWORD     win32; /* Win32 code which reports that failure. */
};

/**
 * @brief The mapping table, ordered by code.
 *
 * A Win32 code which the table does not carry is reported as `kIoFailure`: the
 * table describes the failures the interfaces of `common/` distinguish, and a
 * code outside it is a failure of the call whose class is unknown.
 *
 * The codes of the filesystem and path range are not carried: they describe a
 * decision of the view about a text - a path of another namespace, a target
 * which is no path of the view, a chain which is too deep - and no Win32 call
 * reports such a decision. The Win32 codes which are near them already name
 * another class: `ERROR_PATH_NOT_FOUND` reports a missing entry and not a path
 * the view refuses, which is the general `kNotFound` row.
 *
 * The codes of the embedded resource, payload trailer and zip range are not
 * carried either, for the same reason: they describe the content of a file -
 * a resource a module does not carry, fields which do not hold, an entry name
 * which would escape its folder - and no Win32 call reports such a decision.
 * `ERROR_RESOURCE_NAME_NOT_FOUND` already reports a resource and not the
 * refusal of a payload, so it stays a row of the general `kNotFound`.
 *
 * The codes of the PE image range are not carried for that same reason. The
 * parser reads the headers of an image out of a buffer it was given, so it
 * calls no Win32 function which could report a refused header: a signature
 * which is not there, a table which runs past the buffer and a section which
 * covers no RVA are decisions about the content of a file. `ERROR_BAD_EXE_FORMAT`
 * is not one of them either, because it reports the answer of the loader to an
 * image it refused to map and not the answer of this parser.
 *
 * The codes of the RPC layer are not carried for that same reason: a frame
 * which is not the protocol of the run and a response which names no request
 * are decisions about the transport of the run, and the Win32 codes near them
 * (`ERROR_PIPE_BUSY`, `ERROR_PIPE_NOT_CONNECTED`, `ERROR_BROKEN_PIPE`) already
 * name another class of failure. `kSuccess` carries no row either, because it
 * is not a failure: `Win32FromErrorCode()` answers `ERROR_SUCCESS` for it the
 * same way it does for a code no Win32 code produces.
 *
 * The codes of the text range are not carried except for
 * `kTextConversionFailed`: a null pointer a caller passed and a prefix a text
 * does not start with are decisions of the comparison itself, while
 * `ERROR_NO_UNICODE_TRANSLATION` is the answer the system gives to a text the
 * code page cannot carry.
 */
inline constexpr Win32ErrorMapping kWin32ErrorMappings[] = {
    /* General failures. */
    { ErrorCode::kInvalidArgument,      ERROR_INVALID_PARAMETER      },
    { ErrorCode::kInvalidArgument,      ERROR_INVALID_HANDLE         },
    { ErrorCode::kInvalidArgument,      ERROR_INVALID_NAME           },
    { ErrorCode::kInvalidArgument,      ERROR_BAD_ARGUMENTS          },
    { ErrorCode::kNullPointer,          ERROR_INVALID_ADDRESS        },
    { ErrorCode::kBufferTooSmall,       ERROR_INSUFFICIENT_BUFFER    },
    { ErrorCode::kBufferTooSmall,       ERROR_BUFFER_OVERFLOW        },
    { ErrorCode::kBufferTooSmall,       ERROR_MORE_DATA              },
    { ErrorCode::kNotFound,             ERROR_FILE_NOT_FOUND         },
    { ErrorCode::kNotFound,             ERROR_PATH_NOT_FOUND         },
    { ErrorCode::kNotFound,             ERROR_NOT_FOUND              },
    { ErrorCode::kNotFound,             ERROR_NO_MORE_FILES          },
    { ErrorCode::kAlreadyExists,        ERROR_ALREADY_EXISTS         },
    { ErrorCode::kAlreadyExists,        ERROR_FILE_EXISTS            },
    { ErrorCode::kAccessDenied,         ERROR_ACCESS_DENIED          },
    { ErrorCode::kAccessDenied,         ERROR_PRIVILEGE_NOT_HELD     },
    { ErrorCode::kAccessDenied,         ERROR_SHARING_VIOLATION      },
    { ErrorCode::kAccessDenied,         ERROR_LOCK_VIOLATION         },
    { ErrorCode::kOutOfMemory,          ERROR_NOT_ENOUGH_MEMORY      },
    { ErrorCode::kOutOfMemory,          ERROR_OUTOFMEMORY            },
    { ErrorCode::kOutOfMemory,          ERROR_NOT_ENOUGH_QUOTA       },
    { ErrorCode::kOutOfMemory,          ERROR_NO_SYSTEM_RESOURCES    },
    { ErrorCode::kNotSupported,         ERROR_NOT_SUPPORTED          },
    { ErrorCode::kNotSupported,         ERROR_CALL_NOT_IMPLEMENTED   },
    { ErrorCode::kNotSupported,         ERROR_INVALID_FUNCTION       },
    { ErrorCode::kInvalidState,         ERROR_INVALID_STATE          },
    { ErrorCode::kInvalidState,         ERROR_BUSY                   },
    { ErrorCode::kInvalidState,         ERROR_PIPE_NOT_CONNECTED     },
    { ErrorCode::kTimeout,              ERROR_TIMEOUT                },
    { ErrorCode::kTimeout,              ERROR_SEM_TIMEOUT            },
    { ErrorCode::kTimeout,              WAIT_TIMEOUT                 },
    { ErrorCode::kCancelled,            ERROR_OPERATION_ABORTED      },
    { ErrorCode::kCancelled,            ERROR_CANCELLED              },
    { ErrorCode::kIoFailure,            ERROR_READ_FAULT             },
    { ErrorCode::kIoFailure,            ERROR_WRITE_FAULT            },
    { ErrorCode::kIoFailure,            ERROR_IO_DEVICE              },
    { ErrorCode::kIoFailure,            ERROR_CRC                    },
    { ErrorCode::kIoFailure,            ERROR_BROKEN_PIPE            },
    { ErrorCode::kIoFailure,            ERROR_NO_DATA                },
    { ErrorCode::kIoFailure,            ERROR_INVALID_DATA           },

    /* Text. */
    { ErrorCode::kTextConversionFailed, ERROR_NO_UNICODE_TRANSLATION },
};

/**
 * @brief Map a Win32 code onto the code of this table.
 *
 * A code the table does not carry is reported as `kIoFailure`, which is the
 * class of a failed call whose reason this table does not name. `ERROR_SUCCESS`
 * is not a failure, so it maps onto no code and never onto `kSuccess`; the call
 * asks separately with `IsSuccessWin32Error()`.
 *
 * @param[in] error Win32 code, as `GetLastError()` reports it.
 * @return The code of this table which describes the failure.
 */
constexpr ErrorCode ErrorCodeFromWin32(DWORD error) noexcept
{
    for (const auto& mapping : kWin32ErrorMappings)
    {
        if (mapping.win32 == error)
        {
            return mapping.code;
        }
    }

    return ErrorCode::kIoFailure;
}

/**
 * @brief Map a code of this table onto one Win32 code which reports it.
 *
 * The mapping is many to one, so the answer is one of the Win32 codes which
 * produce the code: the first row of the code in the table. A code no Win32
 * code produces answers `ERROR_SUCCESS`, which is the value a caller uses for
 * "no Win32 code describes this failure".
 *
 * @param[in] code Code of this table.
 * @return A Win32 code which reports the code, `ERROR_SUCCESS` for a code no
 *         Win32 code produces.
 */
constexpr DWORD Win32FromErrorCode(ErrorCode code) noexcept
{
    for (const auto& mapping : kWin32ErrorMappings)
    {
        if (mapping.code == code)
        {
            return mapping.win32;
        }
    }

    return ERROR_SUCCESS;
}

/**
 * @brief Whether a Win32 code reports success.
 * @param[in] error Win32 code, as `GetLastError()` reports it.
 * @return true when the code is `ERROR_SUCCESS`.
 */
constexpr bool IsSuccessWin32Error(DWORD error) noexcept
{
    return error == ERROR_SUCCESS;
}

} // namespace appbox

#endif // APPBOX_COMMON_ERROR_CODE_HPP
