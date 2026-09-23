#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "registry/IsolationPolicy.hpp"
#include "registry/IsolationTable.hpp"
#include <gtest/gtest.h>

using appbox::RegistryIsolation;
using appbox::registry::OpenFallback;

namespace
{

/**
 * @brief The isolation modes of an entry, in the order of the decision table.
 */
const RegistryIsolation kModes[] = { RegistryIsolation::Full, RegistryIsolation::WriteCopy, RegistryIsolation::Hide };

} // namespace

/**
 * @brief The fallback table of the three isolation modes.
 *
 * `Full` and `Hide` keep the host entry invisible for every access kind, and
 * `WriteCopy` reads through the host registry or copies the key up into the
 * hive, depending on the access mask.
 */
TEST(UnitRegistryIsolationPolicy, FallbackTable)
{
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::Full, KEY_READ),
              OpenFallback::ReportHiveFailure);
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::Full, KEY_ALL_ACCESS),
              OpenFallback::ReportHiveFailure);

    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::Hide, KEY_READ),
              OpenFallback::ReportHiveFailure);
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::Hide, KEY_SET_VALUE),
              OpenFallback::ReportHiveFailure);

    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::WriteCopy, KEY_READ), OpenFallback::UseHost);
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::WriteCopy, KEY_QUERY_VALUE),
              OpenFallback::UseHost);
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::WriteCopy, KEY_SET_VALUE), OpenFallback::CopyUp);
    EXPECT_EQ(appbox::registry::FallbackForKey(RegistryIsolation::WriteCopy, KEY_ALL_ACCESS), OpenFallback::CopyUp);
}

/**
 * @brief The modes whose fallback reports the hive failure hide the host entry.
 *
 * The rule of `Full` and `Hide` has to agree with the filter which the merged
 * enumeration and the merged counts apply to the host layer, so the two
 * decisions cannot drift apart.
 */
TEST(UnitRegistryIsolationPolicy, FallbackAgreesWithHidesHost)
{
    for (const auto mode : kModes)
    {
        const bool reports_failure =
            appbox::registry::FallbackForKey(mode, KEY_READ) == OpenFallback::ReportHiveFailure;
        EXPECT_EQ(reports_failure, appbox::registry::IsolationTable::HidesHost(mode));
    }
}

/**
 * @brief A read only mask is the only access kind which is reported as a read.
 */
TEST(UnitRegistryIsolationPolicy, RequestsWriteReadOnly)
{
    EXPECT_FALSE(appbox::registry::RequestsWrite(0));
    EXPECT_FALSE(appbox::registry::RequestsWrite(KEY_QUERY_VALUE));
    EXPECT_FALSE(appbox::registry::RequestsWrite(KEY_ENUMERATE_SUB_KEYS));
    EXPECT_FALSE(appbox::registry::RequestsWrite(KEY_NOTIFY));
    EXPECT_FALSE(appbox::registry::RequestsWrite(READ_CONTROL));
    EXPECT_FALSE(appbox::registry::RequestsWrite(SYNCHRONIZE));
    EXPECT_FALSE(appbox::registry::RequestsWrite(KEY_READ));
}

/**
 * @brief Every right which can modify the key counts as a write.
 */
TEST(UnitRegistryIsolationPolicy, RequestsWriteMask)
{
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_SET_VALUE));
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_CREATE_SUB_KEY));
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_CREATE_LINK));
    EXPECT_TRUE(appbox::registry::RequestsWrite(DELETE));
    EXPECT_TRUE(appbox::registry::RequestsWrite(WRITE_DAC));
    EXPECT_TRUE(appbox::registry::RequestsWrite(WRITE_OWNER));
    EXPECT_TRUE(appbox::registry::RequestsWrite(MAXIMUM_ALLOWED));
    EXPECT_TRUE(appbox::registry::RequestsWrite(GENERIC_WRITE));
    EXPECT_TRUE(appbox::registry::RequestsWrite(GENERIC_ALL));

    /* The combined masks of the API. */
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_WRITE));
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_ALL_ACCESS));

    /* A read which is widened by a single write right counts as a write. */
    EXPECT_TRUE(appbox::registry::RequestsWrite(KEY_READ | KEY_SET_VALUE));
}

/**
 * @brief Only the two statuses of a missing key are reported as not found.
 */
TEST(UnitRegistryIsolationPolicy, IsKeyNotFound)
{
    EXPECT_TRUE(appbox::registry::IsKeyNotFound(STATUS_OBJECT_NAME_NOT_FOUND));
    EXPECT_TRUE(appbox::registry::IsKeyNotFound(STATUS_OBJECT_PATH_NOT_FOUND));

    EXPECT_FALSE(appbox::registry::IsKeyNotFound(STATUS_SUCCESS));
    EXPECT_FALSE(appbox::registry::IsKeyNotFound(STATUS_ACCESS_DENIED));
    EXPECT_FALSE(appbox::registry::IsKeyNotFound(STATUS_NO_MORE_ENTRIES));
    EXPECT_FALSE(appbox::registry::IsKeyNotFound(STATUS_BUFFER_TOO_SMALL));
    EXPECT_FALSE(appbox::registry::IsKeyNotFound(STATUS_INVALID_PARAMETER));
}

/**
 * @brief The failure of an open which both layers refused.
 *
 * A failure which is not a not-found result describes a key which exists but
 * is not accessible, so it wins over a missing key. When both layers report a
 * missing key the real registry decides, which keeps the error code of the
 * caller stable compared to a sandbox which does not isolate the key.
 */
TEST(UnitRegistryIsolationPolicy, PickOpenFailure)
{
    /* Both layers report a missing key: the real registry decides. */
    EXPECT_EQ(appbox::registry::PickOpenFailure(STATUS_OBJECT_NAME_NOT_FOUND, STATUS_OBJECT_PATH_NOT_FOUND),
              STATUS_OBJECT_NAME_NOT_FOUND);
    EXPECT_EQ(appbox::registry::PickOpenFailure(STATUS_OBJECT_PATH_NOT_FOUND, STATUS_OBJECT_NAME_NOT_FOUND),
              STATUS_OBJECT_PATH_NOT_FOUND);

    /* An access denial is more informative than a missing key, in both
     * directions. */
    EXPECT_EQ(appbox::registry::PickOpenFailure(STATUS_ACCESS_DENIED, STATUS_OBJECT_NAME_NOT_FOUND),
              STATUS_ACCESS_DENIED);
    EXPECT_EQ(appbox::registry::PickOpenFailure(STATUS_OBJECT_NAME_NOT_FOUND, STATUS_ACCESS_DENIED),
              STATUS_ACCESS_DENIED);
    EXPECT_EQ(appbox::registry::PickOpenFailure(STATUS_ACCESS_DENIED, STATUS_OBJECT_PATH_NOT_FOUND),
              STATUS_ACCESS_DENIED);
}

/**
 * @brief The disposition of a create follows the merged view.
 *
 * A key which only the host holds exists in the view of `WriteCopy`, so the
 * create has to report it as existing; the same key is new for `Full` and
 * `Hide`, which keep the host entry invisible. A key which the hive already
 * holds is reported by the hive for every mode.
 */
TEST(UnitRegistryIsolationPolicy, ViewCreateDisposition)
{
    /* The hive holds the key: the hive wins the merged view, so the host layer
     * is not consulted at all. */
    for (const auto mode : kModes)
    {
        EXPECT_EQ(appbox::registry::ViewCreateDisposition(mode, REG_OPENED_EXISTING_KEY, false),
                  static_cast<ULONG>(REG_OPENED_EXISTING_KEY));
        EXPECT_EQ(appbox::registry::ViewCreateDisposition(mode, REG_OPENED_EXISTING_KEY, true),
                  static_cast<ULONG>(REG_OPENED_EXISTING_KEY));
    }

    /* The hive created the key and the host does not hold it: the key is new
     * for every mode. */
    for (const auto mode : kModes)
    {
        EXPECT_EQ(appbox::registry::ViewCreateDisposition(mode, REG_CREATED_NEW_KEY, false),
                  static_cast<ULONG>(REG_CREATED_NEW_KEY));
    }

    /* The hive created the key and the host holds it: the visible host entry
     * makes the key an existing one for `WriteCopy` only. */
    EXPECT_EQ(appbox::registry::ViewCreateDisposition(RegistryIsolation::WriteCopy, REG_CREATED_NEW_KEY, true),
              static_cast<ULONG>(REG_OPENED_EXISTING_KEY));
    EXPECT_EQ(appbox::registry::ViewCreateDisposition(RegistryIsolation::Full, REG_CREATED_NEW_KEY, true),
              static_cast<ULONG>(REG_CREATED_NEW_KEY));
    EXPECT_EQ(appbox::registry::ViewCreateDisposition(RegistryIsolation::Hide, REG_CREATED_NEW_KEY, true),
              static_cast<ULONG>(REG_CREATED_NEW_KEY));
}

/**
 * @brief The create disposition and the host visibility of a mode agree.
 *
 * The rule of the create entry point has to agree with the filter which the
 * open path, the merged enumeration and the merged counts apply to the host
 * layer: a mode which hides the host entry reports a creation for a key which
 * only the host holds, every other mode reports the key as existing. The two
 * decisions must not drift apart.
 */
TEST(UnitRegistryIsolationPolicy, ViewCreateDispositionAgreesWithHidesHost)
{
    for (const auto mode : kModes)
    {
        const bool reports_existing =
            appbox::registry::ViewCreateDisposition(mode, REG_CREATED_NEW_KEY, true)
            == static_cast<ULONG>(REG_OPENED_EXISTING_KEY);
        EXPECT_EQ(reports_existing, !appbox::registry::IsolationTable::HidesHost(mode));
    }
}

/**
 * @brief The delete route of the merged view.
 *
 * A delete never reaches the real registry: the entry of the hive is removed
 * and a visible entry of the host is recorded as deleted (a whiteout), so the
 * read through does not resurrect it. An entry which only the host holds does
 * not exist in the view of a mode which hides the host entry, so the delete
 * reports the failure of the hive layer — the same answer the open of that
 * entry gives.
 */
TEST(UnitRegistryIsolationPolicy, DeleteOutcome)
{
    using appbox::registry::DeleteOutcomeOf;
    using appbox::registry::DeleteTarget;

    /* Neither layer holds the entry. */
    for (const auto mode : kModes)
    {
        EXPECT_EQ(DeleteOutcomeOf(mode, false, false), DeleteTarget::ReportMissing);
    }

    /* Only the hive holds the entry: it is deleted there, whatever the mode
     * says about the host layer. */
    for (const auto mode : kModes)
    {
        EXPECT_EQ(DeleteOutcomeOf(mode, true, false), DeleteTarget::HiveOnly);
    }

    /* Only the host holds the entry: it is visible for `WriteCopy`, which the
     * whiteout records, and invisible for `Full` and `Hide`. */
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::WriteCopy, false, true), DeleteTarget::WhiteoutOnly);
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::Full, false, true), DeleteTarget::ReportMissing);
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::Hide, false, true), DeleteTarget::ReportMissing);

    /* Both layers hold the entry: the hive entry is deleted and the visible
     * host entry is whited out. */
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::WriteCopy, true, true), DeleteTarget::HiveAndWhiteout);
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::Full, true, true), DeleteTarget::HiveOnly);
    EXPECT_EQ(DeleteOutcomeOf(RegistryIsolation::Hide, true, true), DeleteTarget::HiveOnly);
}

/**
 * @brief A delete records a whiteout exactly when the host entry stays visible.
 *
 * The rule has to agree with the filter which the open path, the merged
 * enumeration and the merged counts apply to the host layer, so the two
 * decisions cannot drift apart.
 */
TEST(UnitRegistryIsolationPolicy, DeleteOutcomeAgreesWithHidesHost)
{
    for (const auto mode : kModes)
    {
        const bool whiteout =
            appbox::registry::DeleteOutcomeOf(mode, false, true) == appbox::registry::DeleteTarget::WhiteoutOnly;
        EXPECT_EQ(whiteout, !appbox::registry::IsolationTable::HidesHost(mode));
    }
}
