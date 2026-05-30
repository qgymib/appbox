#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/Log.hpp"
#include "NtSetInformationFile.hpp"
#include "__init__.hpp"
#include <detours.h>

T_NtSetInformationFile sys_NtSetInformationFile = nullptr;

struct FileInformationClassMap
{
    FILE_INFORMATION_CLASS FileInformationClass;
    const char*            name;
};

static const FileInformationClassMap s_FileInformationClassMap[] = {
    { FileDirectoryInformation,                     "FileDirectoryInformation"                     },
    { FileFullDirectoryInformation,                 "FileFullDirectoryInformation"                 },
    { FileBothDirectoryInformation,                 "FileBothDirectoryInformation"                 },
    { FileBasicInformation,                         "FileBasicInformation"                         },
    { FileStandardInformation,                      "FileStandardInformation"                      },
    { FileInternalInformation,                      "FileInternalInformation"                      },
    { FileEaInformation,                            "FileEaInformation"                            },
    { FileAccessInformation,                        "FileAccessInformation"                        },
    { FileNameInformation,                          "FileNameInformation"                          },
    { FileRenameInformation,                        "FileRenameInformation"                        },
    { FileLinkInformation,                          "FileLinkInformation"                          },
    { FileNamesInformation,                         "FileNamesInformation"                         },
    { FileDispositionInformation,                   "FileDispositionInformation"                   },
    { FilePositionInformation,                      "FilePositionInformation"                      },
    { FileFullEaInformation,                        "FileFullEaInformation"                        },
    { FileModeInformation,                          "FileModeInformation"                          },
    { FileAlignmentInformation,                     "FileAlignmentInformation"                     },
    { FileAllInformation,                           "FileAllInformation"                           },
    { FileAllocationInformation,                    "FileAllocationInformation"                    },
    { FileEndOfFileInformation,                     "FileEndOfFileInformation"                     },
    { FileAlternateNameInformation,                 "FileAlternateNameInformation"                 },
    { FileStreamInformation,                        "FileStreamInformation"                        },
    { FilePipeInformation,                          "FilePipeInformation"                          },
    { FilePipeLocalInformation,                     "FilePipeLocalInformation"                     },
    { FilePipeRemoteInformation,                    "FilePipeRemoteInformation"                    },
    { FileMailslotQueryInformation,                 "FileMailslotQueryInformation"                 },
    { FileMailslotSetInformation,                   "FileMailslotSetInformation"                   },
    { FileCompressionInformation,                   "FileCompressionInformation"                   },
    { FileObjectIdInformation,                      "FileObjectIdInformation"                      },
    { FileCompletionInformation,                    "FileCompletionInformation"                    },
    { FileMoveClusterInformation,                   "FileMoveClusterInformation"                   },
    { FileQuotaInformation,                         "FileQuotaInformation"                         },
    { FileReparsePointInformation,                  "FileReparsePointInformation"                  },
    { FileNetworkOpenInformation,                   "FileNetworkOpenInformation"                   },
    { FileAttributeTagInformation,                  "FileAttributeTagInformation"                  },
    { FileTrackingInformation,                      "FileTrackingInformation"                      },
    { FileIdBothDirectoryInformation,               "FileIdBothDirectoryInformation"               },
    { FileIdFullDirectoryInformation,               "FileIdFullDirectoryInformation"               },
    { FileValidDataLengthInformation,               "FileValidDataLengthInformation"               },
    { FileShortNameInformation,                     "FileShortNameInformation"                     },
    { FileIoCompletionNotificationInformation,      "FileIoCompletionNotificationInformation"      },
    { FileIoStatusBlockRangeInformation,            "FileIoStatusBlockRangeInformation"            },
    { FileIoPriorityHintInformation,                "FileIoPriorityHintInformation"                },
    { FileSfioReserveInformation,                   "FileSfioReserveInformation"                   },
    { FileSfioVolumeInformation,                    "FileSfioVolumeInformation"                    },
    { FileHardLinkInformation,                      "FileHardLinkInformation"                      },
    { FileProcessIdsUsingFileInformation,           "FileProcessIdsUsingFileInformation"           },
    { FileNormalizedNameInformation,                "FileNormalizedNameInformation"                },
    { FileNetworkPhysicalNameInformation,           "FileNetworkPhysicalNameInformation"           },
    { FileIdGlobalTxDirectoryInformation,           "FileIdGlobalTxDirectoryInformation"           },
    { FileIsRemoteDeviceInformation,                "FileIsRemoteDeviceInformation"                },
    { FileAttributeCacheInformation,                "FileAttributeCacheInformation"                },
    { FileNumaNodeInformation,                      "FileNumaNodeInformation"                      },
    { FileStandardLinkInformation,                  "FileStandardLinkInformation"                  },
    { FileRemoteProtocolInformation,                "FileRemoteProtocolInformation"                },
    { FileRenameInformationBypassAccessCheck,       "FileRenameInformationBypassAccessCheck"       },
    { FileLinkInformationBypassAccessCheck,         "FileLinkInformationBypassAccessCheck"         },
    { FileVolumeNameInformation,                    "FileVolumeNameInformation"                    },
    { FileIdInformation,                            "FileIdInformation"                            },
    { FileIdExtdDirectoryInformation,               "FileIdExtdDirectoryInformation"               },
    { FileReplaceCompletionInformation,             "FileReplaceCompletionInformation"             },
    { FileHardLinkFullIdInformation,                "FileHardLinkFullIdInformation"                },
    { FileIdExtdBothDirectoryInformation,           "FileIdExtdBothDirectoryInformation"           },
    { FileDispositionInformationEx,                 "FileDispositionInformationEx"                 },
    { FileRenameInformationEx,                      "FileRenameInformationEx"                      },
    { FileRenameInformationExBypassAccessCheck,     "FileRenameInformationExBypassAccessCheck"     },
    { FileDesiredStorageClassInformation,           "FileDesiredStorageClassInformation"           },
    { FileStatInformation,                          "FileStatInformation"                          },
    { FileMemoryPartitionInformation,               "FileMemoryPartitionInformation"               },
    { FileStatLxInformation,                        "FileStatLxInformation"                        },
    { FileCaseSensitiveInformation,                 "FileCaseSensitiveInformation"                 },
    { FileLinkInformationEx,                        "FileLinkInformationEx"                        },
    { FileLinkInformationExBypassAccessCheck,       "FileLinkInformationExBypassAccessCheck"       },
    { FileStorageReserveIdInformation,              "FileStorageReserveIdInformation"              },
    { FileCaseSensitiveInformationForceAccessCheck, "FileCaseSensitiveInformationForceAccessCheck" },
};

static nlohmann::json FileInformationClassToJson(FILE_INFORMATION_CLASS FileInformationClass)
{
    for (auto& item : s_FileInformationClassMap)
    {
        if (item.FileInformationClass == FileInformationClass)
        {
            return item.name;
        }
    }
    return FileInformationClass;
}

static nlohmann::json NtSetInformationFileLogParam(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock,
                                                   PVOID FileInformation, ULONG Length,
                                                   FILE_INFORMATION_CLASS FileInformationClass)
{
    nlohmann::json param;
    param["FileHandle"] = appbox::PointerToString(FileHandle);
    param["IoStatusBlock"] = appbox::PointerToString(IoStatusBlock);
    param["FileInformation"] = appbox::PointerToString(FileInformation);
    param["Length"] = Length;
    param["FileInformationClass"] = FileInformationClassToJson(FileInformationClass);
    return param;
}

static appbox::LoggerF logger("NtSetInformationFile", NtSetInformationFileLogParam);

static NTSTATUS Hook_NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,
                                          ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    logger.Log(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
    return sys_NtSetInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

void appbox::AttachNtSetInformationFile()
{
    auto addr = GetProcAddress(sys.h_ntdll, "NtSetInformationFile");
    sys_NtSetInformationFile = reinterpret_cast<T_NtSetInformationFile>(addr);

    DetourAttach(&sys_NtSetInformationFile, Hook_NtSetInformationFile);
}

void appbox::DetachNtSetInformationFile()
{
    DetourDetach(&sys_NtSetInformationFile, Hook_NtSetInformationFile);
}
