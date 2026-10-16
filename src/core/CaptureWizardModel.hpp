#ifndef APPBOX_PACKER_CORE_CAPTURE_WIZARD_MODEL_HPP
#define APPBOX_PACKER_CORE_CAPTURE_WIZARD_MODEL_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace appbox
{

/**
 * @brief The way a capture reads the changes of an installation.
 */
enum class CaptureMode
{
    /**
     * @brief The changes of an installation are recorded while it runs.
     */
    Realtime,

    /**
     * @brief The state of the machine is read before the installation and
     *        after it, and the two reads are compared.
     */
    Snapshot,
};

/**
 * @brief The step of the wizard of a capture.
 *
 * The steps are ordered: a step which comes before the current one is over,
 * and a step which comes after it was not reached yet. A mode does not use
 * every step, so the steps a mode walks are the ones CaptureStages() names for
 * it, in the order they are shown.
 */
enum class CaptureWizardStage
{
    /**
     * @brief Choose the way the capture reads the changes.
     */
    Mode,

    /**
     * @brief Pick the baseline the snapshot capture compares the machine with.
     */
    Snapshot,

    /**
     * @brief Pick the file the baseline of a snapshot capture is kept in.
     *
     * The step only names the file: the work of the source which was picked is
     * asked for by the step which follows, so the step is left once and the
     * path the user typed stays with the model.
     */
    SnapshotFile,

    /**
     * @brief Read the baseline of a snapshot capture.
     *
     * The step is entered by the step which names the file and asks the owner
     * for the work of the source which was picked: the state of the machine is
     * read into the file, or the state the file holds is accepted. The wizard
     * waits for the baseline before it can go on, and a run which could not
     * produce one sends it back to the step of the file, where the path is
     * fixed and where the work is asked for again.
     */
    Baseline,

    /**
     * @brief The realtime capture records the changes of the machine.
     */
    Recording,

    /**
     * @brief The snapshot capture tells the user to install and waits for the
     *        confirmation that the installation is over.
     */
    Installation,

    /**
     * @brief The snapshot capture reads the state of the machine again and
     *        compares it with the baseline.
     */
    Comparison,

    /**
     * @brief The changes are analyzed and filled on the decision of the user.
     */
    Review,

    /**
     * @brief The capture is over.
     */
    Done,
};

/**
 * @brief What a step of the wizard shows.
 */
enum class CaptureStageState
{
    Done,    ///< The step is behind the wizard.
    Current, ///< The step is the one the wizard shows.
    Pending, ///< The step was not reached yet.
};

/**
 * @brief Where the baseline of a snapshot capture comes from.
 */
enum class SnapshotBaselineSource
{
    /**
     * @brief The state of the machine is read into a new snapshot file.
     */
    New,

    /**
     * @brief The state a snapshot file holds is used as the baseline.
     */
    Existing,
};

/**
 * @brief The prerequisites the realtime capture asks for.
 *
 * The kernel events of the file system and of the registry came with Windows
 * 10, and the session which consumes them answers only to an elevated
 * process, so both are checked before the recording is offered. The snapshot
 * capture reads the machine instead of observing it, so it asks for neither of
 * the two.
 */
struct CaptureRequirements
{
    /**
     * @brief Whether the machine carries the kernel events of the capture.
     */
    bool supported = false;

    /**
     * @brief Whether the process runs with an elevated token.
     */
    bool elevated = false;

    /**
     * @brief Whether every prerequisite is met.
     * @return true when the capture can record on this machine.
     */
    bool Met() const
    {
        return supported && elevated;
    }
};

/**
 * @brief One prerequisite of the realtime capture, the way the wizard shows it.
 */
struct CaptureRequirement
{
    /**
     * @brief Name of the prerequisite, e.g. `Windows 10 or newer`.
     */
    std::wstring name;

    /**
     * @brief What the prerequisite means and what to do when it is not met.
     */
    std::wstring detail;

    /**
     * @brief Whether the machine meets the prerequisite.
     */
    bool met = false;
};

/**
 * @brief Describe the prerequisites of a realtime capture.
 *
 * The list is what the first step of the wizard shows while the realtime
 * capture is picked, so the rules of the capture and the text which explains
 * them stay together and testable.
 *
 * @param[in] requirements The prerequisites to describe.
 * @return One entry per prerequisite, in the order they are shown.
 */
std::vector<CaptureRequirement> DescribeRequirements(const CaptureRequirements& requirements);

/**
 * @brief Get the display names of the capture modes.
 *
 * The list is ordered like the enumeration of the modes, so the position of a
 * name is the mode itself. It is the single source of the names the wizard
 * offers and the names the tests assert.
 *
 * @return The display names, one per mode.
 */
const std::vector<std::wstring>& CaptureModeNames();

/**
 * @brief Get the display name of one capture mode.
 * @param[in] mode The capture mode.
 * @return The display name of the mode.
 */
std::wstring CaptureModeName(CaptureMode mode);

/**
 * @brief Describe a capture mode for the option which picks it.
 *
 * The description says how the mode reads the changes of an installation and
 * what it asks the machine for. It is the text the first step of the wizard
 * shows below the name of the mode, so the explanation of a mode lives in one
 * place and no dialog writes its own. The name of the mode is the label of the
 * option above the text, so the text does not repeat it, exactly like the
 * description of an isolation mode (see `FilesystemIsolationDescription()`).
 *
 * @param[in] mode The capture mode.
 * @return The description of the mode.
 */
std::wstring CaptureModeDescription(CaptureMode mode);

/**
 * @brief Get the steps a capture mode walks, in the order they are shown.
 *
 * The steps of the wizard, of the tree which names them and of the pages which
 * render them are the ones of this list, so a mode which needs more work than
 * another is described by the list alone and neither the view nor the owner of
 * the wizard has to know which mode is picked.
 *
 * @param[in] mode The capture mode.
 * @return The steps of the mode, in the order the wizard shows them.
 */
std::vector<CaptureWizardStage> CaptureStages(CaptureMode mode);

/**
 * @brief The steps of the wizard of a capture.
 *
 * The model holds which step the wizard shows, which steps are over and
 * whether the step the wizard shows may be left: the view renders that state
 * and forwards the clicks of the user, so the rules of the wizard can be
 * tested without a window.
 *
 * A wizard starts on the first step with the mode of a realtime capture
 * picked, because the machine which cannot run it is told so by the state of
 * the mode itself.
 */
class CaptureWizardModel
{
public:
    /**
     * @brief Create the model of a wizard.
     * @param[in] requirements The prerequisites of the realtime capture.
     */
    explicit CaptureWizardModel(const CaptureRequirements& requirements);

    /**
     * @brief The prerequisites the model was created with.
     * @return The prerequisites.
     */
    const CaptureRequirements& Requirements() const;

    /**
     * @brief The mode the wizard picked.
     * @return The mode.
     */
    CaptureMode Mode() const;

    /**
     * @brief Whether a mode can be picked on this machine.
     *
     * A mode is offered while the machine carries what it needs: a machine
     * without the kernel events cannot run a realtime capture at all, so its
     * mode is shown as unavailable instead of being picked. The snapshot
     * capture reads the machine instead of observing it, so it is offered
     * everywhere.
     *
     * @param[in] mode The mode to check.
     * @return true when the mode is offered.
     */
    bool CanSelectMode(CaptureMode mode) const;

    /**
     * @brief Pick a mode.
     *
     * The call is ignored when the mode is not offered, see CanSelectMode().
     * A mode which is picked anew is a capture which starts over: the
     * confirmations of the mode which was left behind are dropped.
     *
     * @param[in] mode The mode to pick.
     * @return true when the mode was picked.
     */
    bool SelectMode(CaptureMode mode);

    /**
     * @brief The steps the picked mode walks, in the order they are shown.
     * @return The steps of the mode.
     */
    std::vector<CaptureWizardStage> Stages() const;

    /**
     * @brief The step the wizard shows.
     * @return The step.
     */
    CaptureWizardStage Stage() const;

    /**
     * @brief The state of one step.
     * @param[in] stage The step to ask for.
     * @return Whether the step is over, the current one or not reached yet. A
     *         step the picked mode does not walk was not reached yet either.
     */
    CaptureStageState StateOf(CaptureWizardStage stage) const;

    /**
     * @brief Whether the wizard can leave the step it shows.
     *
     * The first step is left once the machine meets the prerequisites of the
     * picked mode, the step which picks the source of the baseline once the
     * source is there, which it always is, the step which picks the file of the
     * baseline once a path is set, the step of the baseline once it is there,
     * the step which waits for the installation once the user confirms that it
     * is over, the step of the comparison once the analysis is ready and the
     * step of the analysis and the fill once the whole work of the capture is
     * over.
     *
     * @return true when the step allows the wizard to go on.
     */
    bool CanAdvance() const;

    /**
     * @brief Whether the capture can be dropped right now.
     *
     * The capture can be dropped while its work is still being prepared, and it
     * cannot be dropped while the changes are written into the workspaces and
     * after they were written: the workspaces are part of the session of the
     * user, so a capture which reached that point is a capture which is over.
     *
     * @return true when the user can still cancel the capture.
     */
    bool CanCancel() const;

    /**
     * @brief Leave the step the wizard shows.
     * @return true when the wizard moved to the next step.
     */
    bool Advance();

    /**
     * @brief The label of the button which leaves the step the wizard shows.
     * @return The label.
     */
    std::wstring AdvanceLabel() const;

    /**
     * @brief Tell the model whether the user confirmed that the installation
     *        the capture follows is over.
     *
     * The step of the recording and the step of the installation wait for the
     * confirmation of the user, so the model cannot be left before the changes
     * of the installation are declared to be complete. The confirmation can be
     * taken back, which closes the step again.
     *
     * @param[in] done Whether the user confirmed that the installation is over.
     */
    void SetInstallationDone(bool done);

    /**
     * @brief Whether the user confirmed that the installation is over.
     * @return true after SetInstallationDone(true).
     */
    bool IsInstallationDone() const;

    /**
     * @brief The source the baseline of a snapshot capture is taken from.
     * @return The source.
     */
    SnapshotBaselineSource BaselineSource() const;

    /**
     * @brief Pick the source of the baseline of a snapshot capture.
     *
     * The call is ignored unless the snapshot capture is the mode of the
     * wizard, see Mode(). Picking a source asks for the work of that source
     * again, so a baseline which was ready is dropped: the wizard waits for
     * the new one before it can be left.
     *
     * @param[in] source The source to pick.
     * @return true when the source was picked.
     */
    bool SelectBaselineSource(SnapshotBaselineSource source);

    /**
     * @brief The file the baseline of a snapshot capture is kept in.
     *
     * The path is what the user typed or picked on the step of the file of the
     * baseline: it is empty until the user sets it, and the step cannot be left
     * before it is there.
     *
     * @return The path, empty when the user did not set one.
     */
    const std::wstring& SnapshotPath() const;

    /**
     * @brief Set the file the baseline of a snapshot capture is kept in.
     *
     * A path which is set anew drops a baseline which was ready, because the
     * file the state was read from or written into is not the one the user
     * names anymore.
     *
     * @param[in] path Path the user set, empty when the user cleared it.
     */
    void SetSnapshotPath(const std::wstring& path);

    /**
     * @brief Tell the model how the run of the baseline of a snapshot capture
     *        ended.
     *
     * The step of the baseline waits for the answer: it cannot be left before
     * the state of the machine was read into the new file or the state of the
     * file the user picked was accepted. A run which produced no baseline sends
     * the wizard back to the step which names the file, because that is where
     * the path is fixed and where the work is asked for again.
     *
     * @param[in] ready Whether the baseline is ready.
     */
    void SetBaselineReady(bool ready);

    /**
     * @brief Whether the baseline of a snapshot capture is ready.
     * @return true after SetBaselineReady(true).
     */
    bool IsBaselineReady() const;

    /**
     * @brief Tell the model that the analysis of the changes is over.
     *
     * The step of the comparison waits for the analysis, so the model cannot be
     * left before the difference of the capture is there.
     */
    void MarkAnalysisReady();

    /**
     * @brief Whether the analysis of the changes is over.
     * @return true after MarkAnalysisReady().
     */
    bool IsAnalysisReady() const;

    /**
     * @brief Tell the model whether the changes are written into the workspaces
     *        right now.
     *
     * The step of the analysis and the fill runs the whole pipeline of the
     * capture on its own: it analyzes the difference, repairs the entries and
     * writes them into the workspaces. The last part is the one the user cannot
     * stop, so the model is told while it runs and the view greys the button
     * which drops the capture, see CanCancel().
     *
     * @param[in] running Whether the fill runs right now.
     */
    void SetFillRunning(bool running);

    /**
     * @brief Whether the changes are written into the workspaces right now.
     * @return true after SetFillRunning(true).
     */
    bool IsFillRunning() const;

    /**
     * @brief Tell the model that the whole work of the capture is over.
     *
     * The step of the analysis and the fill is the last step of both capture
     * modes: its work is the analysis of the difference, the repairs of the
     * entries and the write of the workspaces, and the step cannot be left
     * before all of them are over, see CanAdvance().
     */
    void MarkFillComplete();

    /**
     * @brief Whether the whole work of the capture is over.
     * @return true after MarkFillComplete().
     */
    bool IsFillComplete() const;

    /**
     * @brief Whether the capture is over.
     * @return true when the wizard shows the last step.
     */
    bool IsFinished() const;

private:
    /**
     * @brief Find a step in the steps of the picked mode.
     * @param[in] stage The step to look for.
     * @return The position of the step, the number of steps when the mode does
     *         not walk it.
     */
    std::size_t StageIndex(CaptureWizardStage stage) const;

    /**
     * @brief Whether the machine carries what the picked mode asks for.
     * @return true when the mode can run on this machine.
     */
    bool ModeReady() const;

    /**
     * @brief Show the step which names the file of the baseline again.
     *
     * The step of the baseline is entered by the step which names the file, so
     * anything which makes the run of the baseline meaningless sends the wizard
     * back to it: the path is fixed there and the work is asked for again. A
     * wizard which shows another step is left where it is.
     */
    void LeaveBaseline();

    /**
     * @brief The prerequisites of the realtime capture.
     */
    CaptureRequirements requirements_;

    /**
     * @brief The mode the wizard picked.
     */
    CaptureMode mode_ = CaptureMode::Realtime;

    /**
     * @brief The step the wizard shows.
     */
    CaptureWizardStage stage_ = CaptureWizardStage::Mode;

    /**
     * @brief The source the baseline of a snapshot capture is taken from.
     */
    SnapshotBaselineSource baseline_source_ = SnapshotBaselineSource::New;

    /**
     * @brief The file the baseline of a snapshot capture is kept in.
     */
    std::wstring snapshot_path_;

    /**
     * @brief Whether the user confirmed that the installation is over.
     */
    bool installation_done_ = false;

    /**
     * @brief Whether the baseline of a snapshot capture is ready.
     */
    bool baseline_ready_ = false;

    /**
     * @brief Whether the analysis of the changes is over.
     */
    bool analysis_ready_ = false;

    /**
     * @brief Whether the changes are written into the workspaces right now.
     */
    bool fill_running_ = false;

    /**
     * @brief Whether the whole work of the capture is over.
     */
    bool fill_complete_ = false;
};

} // namespace appbox

#endif // APPBOX_PACKER_CORE_CAPTURE_WIZARD_MODEL_HPP
