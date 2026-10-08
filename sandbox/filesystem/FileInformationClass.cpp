#include "utils/WinAPI.h" /* Must be first include file */
#include "FileInformationClass.hpp"

struct FileInformationClassMap
{
    FILE_INFORMATION_CLASS FileInformationClass;
    const char*            name;
};

/**
 * @brief Names of the information classes the file entry points accept.
 *
 * The table is the vocabulary of the trace: a class which is not listed is
 * reported with its numeric value, so a build against a newer SDK keeps
 * logging the calls it does not know by name.
 */
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

nlohmann::json appbox::filesystem::FileInformationClassName(FILE_INFORMATION_CLASS FileInformationClass)
{
    for (const auto& item : s_FileInformationClassMap)
    {
        if (item.FileInformationClass == FileInformationClass)
        {
            return item.name;
        }
    }
    return FileInformationClass;
}

bool appbox::filesystem::SetInformationCarriesPath(FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
    case FileRenameInformation:
    case FileRenameInformationEx:
    case FileRenameInformationBypassAccessCheck:
    case FileRenameInformationExBypassAccessCheck:
    case FileLinkInformation:
    case FileLinkInformationEx:
    case FileLinkInformationBypassAccessCheck:
    case FileLinkInformationExBypassAccessCheck:
        return true;
    default:
        return false;
    }
}

bool appbox::filesystem::QueryInformationCarriesName(FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
    case FileNameInformation:
    case FileNormalizedNameInformation:
    case FileAllInformation:
        return true;
    default:
        return false;
    }
}
