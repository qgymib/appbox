#ifndef APPBOX_PACKER_CORE_REALTIME_CAPTURE_HPP
#define APPBOX_PACKER_CORE_REALTIME_CAPTURE_HPP

#include "SnapshotApply.hpp"
#include "SnapshotDiff.hpp"
#include "SnapshotModel.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief One resolved change of a realtime capture recording.
 *
 * The recorder turns the kernel events it consumes into the entries below:
 * the file paths are Win32 paths of the host and the registry paths are the
 * paths of the registry view (`L"HKEY_CURRENT_USER\\Software\\..."`), so a
 * change can be handed to the models of the packer without another lookup.
 *
 * A key which is new is not reported on its own: the kernel events cannot
 * tell a key which was created from a key which was only opened, so the keys
 * of the difference are the ones a value was written to.
 */
struct RealtimeCaptureEvent
{
    /**
     * @brief The kind of the change.
     */
    enum class Kind
    {
        FileCreated,  ///< The file or folder came into being during the recording.
        FileWritten,  ///< A file which was there before was written.
        FileRenamed,  ///< A file or folder was renamed into a new path.
        FileDeleted,  ///< A file or folder was deleted.
        ValueSet,     ///< A registry value was written.
        ValueDeleted, ///< A registry value was deleted.
        KeyDeleted,   ///< A registry key was deleted.
    };

    /**
     * @brief The kind of the change.
     */
    Kind kind = Kind::FileWritten;

    /**
     * @brief Leaf name of the image of the process which caused the change.
     */
    std::wstring process;

    /**
     * @brief Win32 path of the file or view path of the registry key.
     */
    std::wstring path;

    /**
     * @brief The new path of a rename or the name of a registry value, empty
     *        for the other kinds.
     */
    std::wstring detail;

    /**
     * @brief Time of the kernel event in 100ns units since 1601-01-01 UTC.
     */
    std::int64_t time = 0;
};

/**
 * @brief The noise a realtime capture leaves out.
 *
 * A capture records the events of every process of the machine, so the noise
 * of the system itself has to be filtered: the packer, the services which
 * rewrite the machine all the time and the places they write to. The tables
 * are part of the options so a test can point them at a process of its own.
 */
struct RealtimeCaptureOptions
{
    /**
     * @brief Leaf names of the images whose events are dropped.
     *
     * The comparison folds the case of the names.
     */
    std::vector<std::wstring> excluded_processes;

    /**
     * @brief Win32 paths whose files and folders are dropped.
     *
     * A path is dropped when one of the entries is a component prefix of it,
     * so `L"C:\\Windows\\Temp"` drops everything below the folder as well.
     */
    std::vector<std::wstring> excluded_file_paths;

    /**
     * @brief View paths of the registry whose keys and values are dropped.
     *
     * A path is dropped when one of the entries is a component prefix of it.
     */
    std::vector<std::wstring> excluded_registry_paths;
};

/**
 * @brief One progress report of a realtime capture recording.
 */
struct RealtimeCaptureProgress
{
    /**
     * @brief Number of changes recorded so far.
     */
    std::size_t events = 0;

    /**
     * @brief The path of the entry which was just recorded, may be empty.
     */
    std::wstring current;

    /**
     * @brief The process which caused the latest change, may be empty.
     */
    std::wstring process;
};

/**
 * @brief Progress callback of a realtime capture.
 *
 * The callback is invoked from the consumer thread of the recorder at a
 * throttled rate; returning false stops the recording with a cancellation.
 */
using RealtimeCaptureProgressCallback = std::function<bool(const RealtimeCaptureProgress&)>;

/**
 * @brief Build the default noise tables of a realtime capture.
 *
 * The list names the packer itself and the services which rewrite the state
 * of the machine on their own, the state the shell of the machine keeps for
 * itself and the places of the host which carry no application content. The
 * shell is not one of the processes: the user works with it, so the entries it
 * creates, renames and removes are changes of the machine, and only the state
 * it rewrites on its own is left out. It is the starting point the main window
 * hands to the recorder, so the capture and the workspace agree on what
 * noise is.
 *
 * @return The options with the built-in tables.
 */
RealtimeCaptureOptions DefaultRealtimeCaptureOptions();

/**
 * @brief Whether an event is noise the options leave out.
 *
 * The check is the one filter of the capture: the recorder applies it before
 * an event is accumulated and the harvest applies it again, so a test can
 * drive it with the events of its own.
 *
 * @param[in] event The event to check.
 * @param[in] options The noise tables.
 * @return true when the event is noise.
 */
bool RealtimeCaptureEventExcluded(const RealtimeCaptureEvent& event, const RealtimeCaptureOptions& options);

/**
 * @brief Translate an NT registry path into the path of the registry view.
 *
 * `\REGISTRY\MACHINE` becomes `HKEY_LOCAL_MACHINE`, the hive of the current
 * user becomes `HKEY_CURRENT_USER` and every other hive below
 * `\REGISTRY\USER` keeps its name below `HKEY_USERS`. A path the view cannot
 * express (the private hives below `\REGISTRY\A`, for example) yields an
 * empty string.
 *
 * @param[in] nt_path The path the kernel reported.
 * @param[in] current_sid The SID of the current user, as text.
 * @return The view path, empty when the path is not part of the view.
 */
std::wstring NtRegistryPathToViewPath(const std::wstring& nt_path, const std::wstring& current_sid);

/**
 * @brief Translate an NT file path into a Win32 path.
 *
 * The kernel reports the files below their device names, so the volume
 * prefixes have to be exchanged for the drive letters of the host. The map
 * is passed in so a test can drive the translation without the volumes of
 * the machine.
 *
 * The reported name keeps the separators of the caller, so a folder can arrive
 * with a trailing separator and a joined name with a doubled one: the returned
 * path is canonical, which is what keeps one entry from being described twice.
 *
 * @param[in] nt_path The path the kernel reported, e.g.
 *            `L"\\Device\\HarddiskVolume4\\workspace\\a.txt"`.
 * @param[in] device_map Volume prefixes and their drive letters, e.g.
 *            `L"\\Device\\HarddiskVolume4"` and `L"E:"`.
 * @return The Win32 path, empty when no volume of the map matches.
 */
std::wstring NtFilePathToWin32Path(const std::wstring&                                       nt_path,
                                   const std::vector<std::pair<std::wstring, std::wstring>>& device_map);

/**
 * @brief Whether the kernel providers of the realtime capture exist.
 *
 * The events of the capture come with Windows 10, and the build has to be a
 * release which carries them, so an older machine is told to use the
 * snapshot capture instead.
 *
 * @return true when the realtime capture is supported.
 */
bool RealtimeCaptureSupported();

/**
 * @brief Whether the process runs with an elevated token.
 *
 * The kernel events of the capture need the system profile privilege, which
 * only an elevated process holds, so the window asks the user to restart the
 * packer as an administrator when the check fails.
 *
 * @return true when the token of the process is elevated.
 */
bool IsProcessElevated();

/**
 * @brief One event of the manifest of a provider, as a machine declares it.
 *
 * The table of the events of a provider is what the capture derives its enable
 * parameters from: an event carries the identifier, the level and the keyword
 * its manifest declares, and the capture reads the events it handles from that
 * table instead of naming the level and the keywords of a version of Windows.
 * A machine which declares an event with another level or another keyword is
 * therefore still read, see `docs/Capture.md`.
 */
struct ProviderEvent
{
    /** @brief The event identifier of the manifest. */
    unsigned short id = 0;

    /** @brief The level the event is declared with. */
    unsigned char level = 0;

    /** @brief The keyword the event is declared with. */
    unsigned long long keyword = 0;
};

/**
 * @brief The parameters the file provider is enabled with.
 */
struct ProviderEnable
{
    /**
     * @brief The union of the keywords of the events the capture reads.
     *
     * An event is delivered when one of its keywords was asked for, so the
     * union is what enables every event the capture reads. An event which
     * carries a keyword of the union as well is delivered too; the recording
     * leaves it out where it resolves the events.
     */
    unsigned long long keywords = 0;

    /**
     * @brief The events the capture reads which the table does not declare.
     *
     * A machine whose manifest declares none of them cannot report the change
     * the event names, which is what the caller reports instead of recording a
     * capture with a gap nobody named.
     */
    std::vector<unsigned short> missing;

    /**
     * @brief Whether the table declared every event the capture reads.
     * @return true when no event of the capture is missing.
     */
    bool Complete() const
    {
        return missing.empty();
    }
};

/**
 * @brief The identifiers of the file events the capture reads.
 *
 * The identifiers are the ones the manifest of the provider of the file events
 * of the kernel declares; the capture reads the events which report a file or
 * a folder coming into being, a write, a rename, a deletion and the rundown of
 * the names of the files the kernel holds.
 *
 * @return The event identifiers, in ascending order.
 */
std::vector<unsigned short> RealtimeCaptureFileEventIds();

/**
 * @brief Derive the enable parameters of the file provider from a manifest.
 *
 * The call is the one place which decides what the capture asks the machine
 * for: the keywords of the events the capture reads are joined, so the provider
 * delivers them whatever keyword its manifest gives them, and the events the
 * table does not declare are reported.
 *
 * The provider is enabled at the verbose level, which is the level every event
 * of a machine is delivered with: the level of an event belongs to the manifest
 * of its provider and differs between the versions of Windows, while the
 * keywords are what keeps the noise of the machine out of the recording.
 *
 * @param[in] events The events the machine declares for the provider.
 * @return The parameters the provider is enabled with.
 */
ProviderEnable FileProviderEnableFrom(const std::vector<ProviderEvent>& events);

/**
 * @brief The counters the session of a recording reported.
 *
 * The kernel counts the events it dropped while no consumer drained the buffers
 * of the session, which is the loss a recording has to report: an event which
 * was dropped is a change the recording never saw. The counters belong to the
 * moment the session was stopped, because the session is gone from that moment
 * on.
 */
struct RealtimeCaptureStatistics
{
    /** @brief Whether the session reported the counters. */
    bool available = false;

    /** @brief Number of the events the kernel dropped. */
    unsigned long events_lost = 0;

    /** @brief Number of the buffers the kernel dropped. */
    unsigned long buffers_lost = 0;

    /** @brief Number of the buffers the session held when it was stopped. */
    unsigned long number_of_buffers = 0;

    /** @brief Number of the buffers which were free when the session stopped. */
    unsigned long free_buffers = 0;
};

/**
 * @brief Owns one realtime capture recording.
 *
 * The recorder drives a system logger session of Windows which it opens for
 * itself: the session enables the manifest provider of the file events of the
 * kernel and the classic group of its registry events, consumes their events
 * on a thread of its own and resolves them into the entries of the capture.
 * The events of the session are the ones the kernel emits for every process of
 * the machine, so the recording covers the whole machine and not the processes
 * of the packer alone.
 *
 * The payload of an event is decoded with the trace data helper of Windows by
 * the names of its properties, because the layout of an event belongs to the
 * manifest of its provider and differs between the versions of the machine.
 *
 * The session belongs to the capture alone, so a capture no longer takes the
 * shared kernel logger of the machine away from another tool; a session of a
 * crashed run is stopped before the new one starts, and a run refuses to start
 * while a recording of its own still holds the session.
 *
 * A recorder can carry more than one recording: the state of a recording which
 * ended is given up by the next Start(), which is what makes a capture work
 * again after a cancelled or a confirmed one.
 *
 * The consumer of a recording takes the session before the file provider is
 * enabled: the events a provider writes are carried by the buffers of the
 * session until the consumer drains them, and the session drops the events of
 * a burst which fills those buffers while nothing consumes it. The provider
 * reports the rundown of the names of the files the kernel holds when it is
 * enabled, which is such a burst, so it is enabled only once the consumer
 * runs. The registry group is enabled with the flags of the session, and the
 * rundown it reports at that moment is carried by the buffers, which are sized
 * for it. A recording whose session still lost events reports them.
 */
class RealtimeCaptureRecorder
{
public:
    /**
     * @brief Stop the session and join the consumer thread.
     */
    ~RealtimeCaptureRecorder();

    RealtimeCaptureRecorder(const RealtimeCaptureRecorder&) = delete;
    RealtimeCaptureRecorder& operator=(const RealtimeCaptureRecorder&) = delete;

    /**
     * @brief Create the recorder.
     *
     * The constructor is defined next to the implementation state, so the
     * member which holds it is a complete type wherever a recorder is built.
     */
    RealtimeCaptureRecorder();

    /**
     * @brief Start the recording.
     *
     * The session is created and its consumer thread takes it, which is what
     * the providers are enabled after: an event a provider writes is carried
     * by the buffers of the session until the consumer drains them, and a burst
     * which fills the buffers while nothing consumes the session is dropped,
     * see the documentation of the class. The call therefore returns only when
     * the consumer took the session, so the caller can change the machine from
     * the moment the recording runs.
     *
     * The call fails when the machine does not carry the kernel providers, when
     * the process is not elevated, when the consumer does not take the session
     * or when the providers cannot be enabled, and the error names the reason.
     *
     * A recording which still runs is refused, because the session of the
     * capture carries one recording alone. A recording which ended is given up
     * before the new one starts, together with the events which were not
     * taken, so the recorder records again after a cancelled or a confirmed
     * capture.
     *
     * @param[in] options The noise tables of the recording.
     * @param[in] progress Progress callback, may be empty.
     * @param[out] error Error description on failure.
     * @return true when the recording runs.
     */
    bool Start(const RealtimeCaptureOptions& options, const RealtimeCaptureProgressCallback& progress,
               std::string& error);

    /**
     * @brief Ask the recording to stop.
     *
     * The session is stopped, which makes the consumer thread drain the
     * buffers and return. The call is safe to make from any thread and more
     * than once; it does not wait.
     */
    void RequestStop();

    /**
     * @brief Wait for the consumer thread to end.
     *
     * @param[in] milliseconds How long to wait at most.
     * @return true when the consumer thread ended.
     */
    bool Join(unsigned milliseconds);

    /**
     * @brief Take the events of the recording.
     *
     * The events are the ones the filter of the options kept. The call hands
     * the list over and clears it, and it is meant for the analysis after the
     * consumer thread ended; calling it while the recording runs hands out
     * the part which is resolved already.
     *
     * @return The recorded events.
     */
    std::vector<RealtimeCaptureEvent> TakeEvents();

    /**
     * @brief The counters the session of the recording reported.
     *
     * The counters are the ones the kernel reported when the session was
     * stopped, so the call belongs after RequestStop(): a recording which still
     * runs has not reported them yet. The call is safe to make from any thread
     * and reports the counters of the last recording which was stopped.
     *
     * @return The counters of the recording.
     */
    RealtimeCaptureStatistics Statistics() const;

private:
    /**
     * @brief Give up a recording which ended.
     *
     * The state of a recording which ended is dropped here, so the recorder is
     * idle again and the next recording can take the kernel logger of the
     * machine. The session is stopped before the state goes away, because a
     * consumer which left on its own leaves a session which still runs, and
     * the events of the recording are dropped with the state: a caller takes
     * them before the next recording starts.
     *
     * @return true when the recorder is idle; false while a recording runs.
     */
    bool GiveUpFinishedRecording();

    /**
     * @brief Consume the session on the consumer thread.
     *
     * The trace of the session is opened and its events are resolved until the
     * session is stopped. The session is reported as taken once the trace is
     * open and before the events are processed, which is what the owner waits
     * for before it enables a provider.
     */
    void Consume();

    /**
     * @brief Report that the consumer took the session.
     *
     * The flag is set before the events are processed, because the buffers of
     * the session are drained from that moment on.
     */
    void SignalAttached();

    /**
     * @brief Finish the consumer thread.
     *
     * The report of the recording is written, the number of the events which
     * could not be resolved is named and the owner is woken up. The call is the
     * one place which ends the consumer thread, so every path out of Consume()
     * runs it.
     */
    void FinishConsumer();

    /**
     * @brief Wait until the consumer took the session.
     *
     * @param[in] milliseconds How long to wait at most.
     * @return true when the consumer took the session.
     */
    bool WaitForConsumerAttached(unsigned milliseconds);

    /**
     * @brief Give up the state of a recording which could not start.
     *
     * The session is stopped and the consumer thread is waited for before the
     * state is dropped, because a state which goes away while its consumer
     * still reads it would be a use after free.
     */
    void GiveUpState();

    /**
     * @brief Enable the manifest provider of the file events of the kernel.
     *
     * The provider is enabled with the level and the keywords the manifest of
     * the machine declares for the events the capture reads, so the events
     * arrive whatever the version of the machine declares, see
     * `docs/Capture.md`. The call belongs after the consumer took the session:
     * the provider reports the rundown of the names of the files the kernel
     * holds when it is enabled, and that burst has to arrive while the consumer
     * drains the buffers of the session.
     *
     * The classic group of the registry events is enabled with the flags of the
     * session, which is the one way a private system logger session answers,
     * see Start().
     *
     * @param[out] error Error description on failure.
     * @return true when the provider was enabled.
     */
    bool EnableFileProvider(std::string& error);

    /**
     * @brief Implementation state of the recorder.
     */
    struct State;

    /**
     * @brief Implementation state, null while the recorder is idle.
     */
    std::unique_ptr<State> state_;
};

/**
 * @brief Merge the events of a recording into a snapshot difference.
 *
 * The call is the analysis of the capture: the events are reduced to one
 * state per path, the current content of the host is read for the entries
 * which stay and the persisted environment is compared with the state the
 * recording began from. The difference is the one the snapshot capture
 * produces, so it is applied with the same call and carries the same
 * semantics:
 *
 * - Every entry the recording reported is an entry of the difference: a file
 *   or a folder which was created is added, an entry whose content changed is
 *   modified and an entry which was deleted is removed. A path the recording
 *   reported more than once is reduced to its final state, so a file which was
 *   created and deleted again leaves no entry behind. Nothing is read from the
 *   folder which holds an entry, so the difference carries the changes of the
 *   recording alone.
 * - A file which was renamed is reported as the removal of the old path and
 *   the addition of the new one.
 * - A registry value which was written is reported with the data the host
 *   holds at the time of the analysis, together with the key which holds it.
 *   A value or a key which was deleted is reported as removed.
 * - The environment is the difference of the persisted environment of the
 *   machine and the state the recording began from.
 *
 * A change whose path belongs to no preset directory cannot be expressed by
 * the workspaces, so it is reported to the caller instead of being dropped:
 * the recording observes the whole machine and the user has to see which part
 * of it the capture could not carry, see AddIgnoredNotice().
 *
 * @param[in] events The events of the recording, in any order.
 * @param[in] environment_before The persisted environment the recording
 *                began from, see ReadPersistedEnvironment().
 * @param[in] options The noise tables, applied once more.
 * @param[out] diff The difference the events produced.
 * @param[in,out] report Report which receives the changes which belong to no
 *                preset directory.
 * @param[out] error Error description on failure.
 * @return true when the events were merged; the error names the reason when
 *         the machine cannot be read.
 */
bool HarvestRealtimeEvents(const std::vector<RealtimeCaptureEvent>&     events,
                           const std::vector<SnapshotEnvironmentEntry>& environment_before,
                           const RealtimeCaptureOptions& options, SnapshotDiff& diff, CaptureFillReport& report,
                           std::string& error);

} // namespace appbox

#endif // APPBOX_PACKER_CORE_REALTIME_CAPTURE_HPP
