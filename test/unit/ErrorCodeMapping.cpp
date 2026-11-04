#include <gtest/gtest.h>
#include <windows.h>
#include <cstddef>
#include "ErrorCode.hpp"

namespace
{

/**
 * @brief One expectation of the mapping: a Win32 code and the code it reports.
 */
struct MappingCase
{
    DWORD             win32; /* Win32 code. */
    appbox::ErrorCode code;  /* Code of the table which describes the failure. */
};

/**
 * @brief The cases which pin every group of the mapping.
 *
 * One row per Win32 code the table carries, so a row which is dropped or moved
 * to another code fails a case instead of silently changing the class of a
 * failure a caller relies on.
 */
constexpr MappingCase kExpectations[] = {
    /* General failures. */
    { ERROR_INVALID_PARAMETER,      appbox::ErrorCode::kInvalidArgument      },
    { ERROR_INVALID_HANDLE,         appbox::ErrorCode::kInvalidArgument      },
    { ERROR_INVALID_NAME,           appbox::ErrorCode::kInvalidArgument      },
    { ERROR_BAD_ARGUMENTS,          appbox::ErrorCode::kInvalidArgument      },
    { ERROR_INVALID_ADDRESS,        appbox::ErrorCode::kNullPointer          },
    { ERROR_INSUFFICIENT_BUFFER,    appbox::ErrorCode::kBufferTooSmall       },
    { ERROR_BUFFER_OVERFLOW,        appbox::ErrorCode::kBufferTooSmall       },
    { ERROR_MORE_DATA,              appbox::ErrorCode::kBufferTooSmall       },
    { ERROR_FILE_NOT_FOUND,         appbox::ErrorCode::kNotFound             },
    { ERROR_PATH_NOT_FOUND,         appbox::ErrorCode::kNotFound             },
    { ERROR_NOT_FOUND,              appbox::ErrorCode::kNotFound             },
    { ERROR_NO_MORE_FILES,          appbox::ErrorCode::kNotFound             },
    { ERROR_ALREADY_EXISTS,         appbox::ErrorCode::kAlreadyExists        },
    { ERROR_FILE_EXISTS,            appbox::ErrorCode::kAlreadyExists        },
    { ERROR_ACCESS_DENIED,          appbox::ErrorCode::kAccessDenied         },
    { ERROR_PRIVILEGE_NOT_HELD,     appbox::ErrorCode::kAccessDenied         },
    { ERROR_SHARING_VIOLATION,      appbox::ErrorCode::kAccessDenied         },
    { ERROR_LOCK_VIOLATION,         appbox::ErrorCode::kAccessDenied         },
    { ERROR_NOT_ENOUGH_MEMORY,      appbox::ErrorCode::kOutOfMemory          },
    { ERROR_OUTOFMEMORY,            appbox::ErrorCode::kOutOfMemory          },
    { ERROR_NOT_ENOUGH_QUOTA,       appbox::ErrorCode::kOutOfMemory          },
    { ERROR_NO_SYSTEM_RESOURCES,    appbox::ErrorCode::kOutOfMemory          },
    { ERROR_NOT_SUPPORTED,          appbox::ErrorCode::kNotSupported         },
    { ERROR_CALL_NOT_IMPLEMENTED,   appbox::ErrorCode::kNotSupported         },
    { ERROR_INVALID_FUNCTION,       appbox::ErrorCode::kNotSupported         },
    { ERROR_INVALID_STATE,          appbox::ErrorCode::kInvalidState         },
    { ERROR_BUSY,                   appbox::ErrorCode::kInvalidState         },
    { ERROR_PIPE_NOT_CONNECTED,     appbox::ErrorCode::kInvalidState         },
    { ERROR_TIMEOUT,                appbox::ErrorCode::kTimeout              },
    { ERROR_SEM_TIMEOUT,            appbox::ErrorCode::kTimeout              },
    { WAIT_TIMEOUT,                 appbox::ErrorCode::kTimeout              },
    { ERROR_OPERATION_ABORTED,      appbox::ErrorCode::kCancelled            },
    { ERROR_CANCELLED,              appbox::ErrorCode::kCancelled            },
    { ERROR_READ_FAULT,             appbox::ErrorCode::kIoFailure            },
    { ERROR_WRITE_FAULT,            appbox::ErrorCode::kIoFailure            },
    { ERROR_IO_DEVICE,              appbox::ErrorCode::kIoFailure            },
    { ERROR_CRC,                    appbox::ErrorCode::kIoFailure            },
    { ERROR_BROKEN_PIPE,            appbox::ErrorCode::kIoFailure            },
    { ERROR_NO_DATA,                appbox::ErrorCode::kIoFailure            },
    { ERROR_INVALID_DATA,           appbox::ErrorCode::kIoFailure            },

    /* Text. */
    { ERROR_NO_UNICODE_TRANSLATION, appbox::ErrorCode::kTextConversionFailed },
};

} // namespace

/**
 * @brief Every Win32 code the table carries reports its code of the table.
 */
TEST(Unit_ErrorCodeMapping, EveryWin32CodeReportsItsCode)
{
    for (const auto& expectation : kExpectations)
    {
        EXPECT_EQ(appbox::ErrorCodeFromWin32(expectation.win32), expectation.code)
            << "the Win32 code " << expectation.win32 << " reports another code";
    }
}

/**
 * @brief A Win32 code the table does not carry is a failure of the call whose
 *        class the table does not name, which is the I/O class.
 */
TEST(Unit_ErrorCodeMapping, UnknownWin32CodeIsAnIoFailure)
{
    /* A code the system reserves and no interface of `common/` reports. */
    constexpr DWORD kUnlistedError = 0x4FFF;

    EXPECT_EQ(appbox::ErrorCodeFromWin32(kUnlistedError), appbox::ErrorCode::kIoFailure);
    EXPECT_EQ(appbox::ErrorCodeFromWin32(ERROR_INVALID_EA_NAME), appbox::ErrorCode::kIoFailure);
}

/**
 * @brief A code which the table maps answers a Win32 code which maps back to
 *        the same code, so the mapping is usable in both directions.
 *
 * The reverse mapping is many to one: the answer is the first Win32 code of the
 * code, so the round trip starts at the Win32 side and has to end on the same
 * code, not necessarily on the same Win32 code.
 */
TEST(Unit_ErrorCodeMapping, RoundTripKeepsTheCode)
{
    for (const auto& expectation : kExpectations)
    {
        const DWORD answer = appbox::Win32FromErrorCode(expectation.code);

        EXPECT_NE(answer, ERROR_SUCCESS) << "the code " << static_cast<int>(expectation.code) << " has no Win32 code";
        EXPECT_EQ(appbox::ErrorCodeFromWin32(answer), expectation.code)
            << "the Win32 code " << expectation.win32 << " was answered as " << answer;
    }
}

/**
 * @brief The reverse mapping of a code which several Win32 codes report is the
 *        first row of that code, which is the answer the header documents.
 */
TEST(Unit_ErrorCodeMapping, ReverseMappingAnswersTheFirstRowOfTheCode)
{
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kInvalidArgument), ERROR_INVALID_PARAMETER);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kBufferTooSmall), ERROR_INSUFFICIENT_BUFFER);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kNotFound), ERROR_FILE_NOT_FOUND);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kAlreadyExists), ERROR_ALREADY_EXISTS);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kAccessDenied), ERROR_ACCESS_DENIED);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kOutOfMemory), ERROR_NOT_ENOUGH_MEMORY);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kNotSupported), ERROR_NOT_SUPPORTED);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kTimeout), ERROR_TIMEOUT);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kCancelled), ERROR_OPERATION_ABORTED);
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kIoFailure), ERROR_READ_FAULT);
}

/**
 * @brief A code no Win32 code produces answers `ERROR_SUCCESS`, which is the
 *        value a caller reads as "no Win32 code describes this failure".
 */
TEST(Unit_ErrorCodeMapping, ReverseMappingOfACodeWithoutWin32CodeIsSuccess)
{
    constexpr appbox::ErrorCode kCodesWithoutWin32Code[] = {
        appbox::ErrorCode::kTextInputNull,
        appbox::ErrorCode::kPrefixMismatch,
    };

    for (const auto code : kCodesWithoutWin32Code)
    {
        EXPECT_EQ(appbox::Win32FromErrorCode(code), ERROR_SUCCESS);
    }
}

/**
 * @brief Success is not a failure: the table reports it as success and never as
 *        one of its codes.
 */
TEST(Unit_ErrorCodeMapping, SuccessIsReportedAsSuccess)
{
    EXPECT_TRUE(appbox::IsSuccessWin32Error(ERROR_SUCCESS));
    EXPECT_FALSE(appbox::IsSuccessWin32Error(ERROR_FILE_NOT_FOUND));
}

/**
 * @brief The codes of the table are grouped by domain, so a stage which adds a
 *        code does not renumber the codes which exist already.
 */
TEST(Unit_ErrorCodeMapping, CodesAreGroupedByDomain)
{
    /* General failures occupy 1 to 99. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kInvalidArgument), 1);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kIoFailure), 100);

    /* Text occupies 100 to 199. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kTextInputNull), 100);
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kTextConversionFailed), 101);
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kPrefixMismatch), 102);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kPrefixMismatch), 200);

    /* Filesystem and path occupy 200 to 299. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kIsolationEntryNotListed), 200);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kLayerKeyUnmapped), 300);

    /* Registry, network, environment and the isolation documents occupy 300 to 399. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kIsolationTokenUnknown), 300);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kAddressMalformed), 400);

    /* Embedded resources, zip archives, PE images and the RPC layer occupy 400 to 499. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kEmbeddedResourceMissing), 400);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kZipCancelled), 420);
    EXPECT_GE(static_cast<int>(appbox::ErrorCode::kPeFileUnreadable), 414);
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kForwarderMalformed), 419);
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kRpcFrameMagicInvalid), 420);
    EXPECT_LT(static_cast<int>(appbox::ErrorCode::kPipeStateFailed), 500);

    /* The success value is zero, and the failures start above it. */
    EXPECT_EQ(static_cast<int>(appbox::ErrorCode::kSuccess), 0);
    EXPECT_GT(static_cast<int>(appbox::ErrorCode::kInvalidArgument), 0);

    /* The success value names no failure, so it carries no row of the table. */
    EXPECT_EQ(appbox::Win32FromErrorCode(appbox::ErrorCode::kSuccess), static_cast<DWORD>(ERROR_SUCCESS));
}

/**
 * @brief The codes of the isolation domains describe failures no Win32 call
 *        reports, so the round trip of the table answers `ERROR_SUCCESS` for
 *        them, like it does for `kTextInputNull`.
 */
TEST(Unit_ErrorCodeMapping, IsolationCodesHaveNoWin32Code)
{
    constexpr appbox::ErrorCode kIsolationCodes[] = {
        appbox::ErrorCode::kIsolationEntryNotListed,
        appbox::ErrorCode::kLayerKeyUnmapped,
        appbox::ErrorCode::kIsolationTokenUnknown,
        appbox::ErrorCode::kEntryKindTokenUnknown,
        appbox::ErrorCode::kIsolationDocumentInvalid,
        appbox::ErrorCode::kIsolationDocumentUnsupportedVersion,
        appbox::ErrorCode::kPortEmpty,
        appbox::ErrorCode::kPortNotDecimal,
        appbox::ErrorCode::kPortLeadingZero,
        appbox::ErrorCode::kPortOutOfRange,
        appbox::ErrorCode::kAddressMalformed,
    };

    for (const auto code : kIsolationCodes)
    {
        EXPECT_EQ(appbox::Win32FromErrorCode(code), ERROR_SUCCESS)
            << "the code " << static_cast<int>(code) << " answers a Win32 code";
    }
}
