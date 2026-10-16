#ifndef APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP
#define APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP

#include "EnvironmentModel.hpp"
#include "PackModel.hpp"
#include "RegistryModel.hpp"
#include "SnapshotDiff.hpp"
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief The part of the machine an entry of a difference belongs to.
 */
enum class CaptureFillDomain
{
    Filesystem,  ///< The filesystem of the machine.
    Registry,    ///< The registry of the machine.
    Environment, ///< The environment of the machine.
};

/**
 * @brief One entry the fill did not write, named by its message.
 *
 * The message names the entry the way the workspace does and carries the reason
 * it was not written, so a caller can list the entries without knowing the
 * rules of the fill.
 */
struct CaptureFillNotice
{
    /**
     * @brief The part of the machine the entry belongs to.
     */
    CaptureFillDomain domain = CaptureFillDomain::Filesystem;

    /**
     * @brief Description of the entry and of the reason it was not written.
     */
    std::wstring message;
};

/**
 * @brief What the fill did with one entry.
 */
enum class CaptureFillState
{
    Working,   ///< The entry is being written.
    Succeeded, ///< The entry was written into the models.
    Failed,    ///< The entry could not be written.
};

/**
 * @brief One report of the work of the fill.
 *
 * The fill reports an entry before it is written and again with the result, so
 * a caller can show what is being worked on and how it ended. The report is
 * pure data: the fill never touches a window.
 */
struct CaptureFillProgress
{
    /**
     * @brief The part of the machine the entry belongs to.
     */
    CaptureFillDomain domain = CaptureFillDomain::Filesystem;

    /**
     * @brief What the fill is doing, e.g. `Writing the files of the capture`.
     */
    std::wstring action;

    /**
     * @brief The entry which is being written.
     */
    std::wstring object;

    /**
     * @brief The state of the entry.
     */
    CaptureFillState state = CaptureFillState::Working;

    /**
     * @brief The reason of a failure, empty for every other state.
     */
    std::wstring message;
};

/**
 * @brief What the fill reports while it runs.
 *
 * The callback is called from the thread which runs the fill, so a caller which
 * works on another thread has to hand the report over itself. An empty callback
 * disables the reporting.
 */
using CaptureFillProgressCallback = std::function<void(const CaptureFillProgress&)>;

/**
 * @brief What filling a difference did.
 *
 * The report counts the entries which were written and lists the entries which
 * were not, one list per reason: an entry which cannot be attributed to a
 * preset node or which the models cannot express is ignored, and an entry which
 * the fill could not write is an error. Both lists are capped, because a
 * capture of a whole machine can leave out thousands of entries; the total
 * counts cover the entries the lists do not name.
 */
struct CaptureFillReport
{
    /**
     * @brief Number of the entries which were written into the models.
     */
    std::size_t applied = 0;

    /**
     * @brief The entries which were not written because they cannot be expressed.
     */
    std::vector<CaptureFillNotice> ignored;

    /**
     * @brief Number of the ignored entries, including the ones the list does
     *        not name.
     */
    std::size_t total_ignored = 0;

    /**
     * @brief The entries which could not be written.
     */
    std::vector<CaptureFillNotice> errors;

    /**
     * @brief Number of the errors, including the ones the list does not name.
     */
    std::size_t total_errors = 0;
};

/**
 * @brief Report one entry the capture cannot write.
 *
 * The call is what the analysis of a recording uses for a change it cannot
 * attribute to a preset directory, so both halves of a capture fill the same
 * report and the caller lists the entries once. The list of the report is
 * capped while the count covers every entry, so a caller does not have to know
 * how many entries are shown.
 *
 * @param[in,out] report Report which receives the entry.
 * @param[in] domain The part of the machine the entry belongs to.
 * @param[in] message Description of the entry and of the reason it was not
 *            written.
 */
void AddIgnoredNotice(CaptureFillReport& report, CaptureFillDomain domain, const std::wstring& message);

/**
 * @brief Report one entry the capture could not write.
 *
 * The call is what a caller which stops the fill before it began uses, so the
 * reason of the failure reaches the same list as the entries the fill could not
 * write itself.
 *
 * @param[in,out] report Report which receives the entry.
 * @param[in] domain The part of the machine the entry belongs to.
 * @param[in] message Description of the failure and of its reason.
 */
void AddErrorNotice(CaptureFillReport& report, CaptureFillDomain domain, const std::wstring& message);

/**
 * @brief Write a difference of two states of the machine into the models.
 *
 * The call is the second half of a capture: the difference the recording or the
 * comparison produced is filled into the models of the filesystem, of the
 * registry and of the environment, so the session describes the state the
 * newer snapshot was taken from. It is the strict fill both capture modes end
 * in: an entry is written at the very place it was captured at, and nothing
 * around it is taken along.
 *
 * The rules are:
 *
 * - A file which was added or whose content changed is imported at its own
 *   path below its preset directory. The folders of the way to the file are
 *   created as folders of the view when the model does not hold them yet, so a
 *   capture fills a file below a folder which existed before the capture just
 *   as well as one below a folder it created, and a file of the layer root of a
 *   preset directory has no folder of its own at all. Only the entries of the
 *   difference travel: the content of the folder which holds a file is never
 *   read.
 * - A folder which was added is an entry of the view: the folders of the way to
 *   it and the folder itself are created, and the files below it travel as the
 *   entries of their own which the difference reports.
 * - An entry which the newer state does not hold anymore is hidden: the archive
 *   carries the whiteout marker of the entry, which hides the entry of the host
 *   while the packaged application runs. The markers are reduced first: an
 *   entry below a folder which is gone as well is left out, because the marker
 *   of that folder hides it already.
 * - A registry key which is new is created and a value which is new or whose
 *   data changed is written with the data of the newer state. A key or a value
 *   which is gone is hidden with the `Hide` mode of the entry. The environment
 *   keys of the machine are left out of the registry part, because the
 *   environment workspace is the domain which expresses them.
 * - An environment variable which is new or whose value changed is written with
 *   the modes a variable of the workspace starts with: `Write Copy` and
 *   `Replace`. A variable which is gone cannot be expressed, because a row of
 *   the workspace reports a value and never the absence of a variable.
 * - An entry which is part of the view already is left where it is: a file
 *   which an imported folder carries, an entry which was hidden before and a
 *   file which was imported before are not written twice.
 *
 * The absolute paths of a registry value and of an environment variable are
 * rewritten to the reference of the preset directory they point into, so the
 * archive stays correct on a machine whose folders are somewhere else, see
 * `KnownFolderReference.hpp`.
 *
 * A difference which holds an entry the call cannot interpret is refused before
 * anything is written, which leaves the models untouched.
 *
 * @param[in] diff The difference to write.
 * @param[in,out] model Model which receives the imported files and folders and
 *                the hidden entries.
 * @param[in,out] registry Registry which receives the keys and the values.
 * @param[in,out] environment Environment which receives the variables.
 * @param[out] report What the call did and what it left out.
 * @param[in] progress Callback which receives the work of the fill, may be
 *                empty.
 * @param[out] error Error description when the difference itself is not usable.
 * @return true when the difference was written; false when it holds an entry
 *         which cannot be interpreted, which leaves the models untouched.
 */
bool ApplySnapshotDiff(const SnapshotDiff& diff, PackModel& model, RegistryModel& registry,
                       EnvironmentModel& environment, CaptureFillReport& report,
                       const CaptureFillProgressCallback& progress, std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_SNAPSHOT_APPLY_HPP
