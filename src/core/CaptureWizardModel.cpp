#include "CaptureWizardModel.hpp"
#include <algorithm>

namespace appbox
{

std::vector<CaptureRequirement> DescribeRequirements(const CaptureRequirements& requirements)
{
    std::vector<CaptureRequirement> entries;

    CaptureRequirement supported;
    supported.name = L"Windows 10 or newer";
    supported.detail = requirements.supported
                           ? L"The kernel events of the file system and of the registry are available."
                           : L"The machine does not carry the kernel events of the capture. Pick the snapshot capture "
                             L"mode instead: it reads the state of the machine twice and compares the two reads.";
    supported.met = requirements.supported;
    entries.push_back(std::move(supported));

    CaptureRequirement elevated;
    elevated.name = L"Elevated process";
    elevated.detail = requirements.elevated
                          ? L"The process holds the privilege the kernel session answers to."
                          : L"The kernel session of the machine answers only to an elevated process. Restart AppBox "
                            L"as an administrator to record the changes, or pick the snapshot capture mode, which "
                            L"needs no elevation.";
    elevated.met = requirements.elevated;
    entries.push_back(std::move(elevated));

    return entries;
}

const std::vector<std::wstring>& CaptureModeNames()
{
    static const std::vector<std::wstring> names = { L"Realtime capture", L"Snapshot capture" };
    return names;
}

std::wstring CaptureModeName(CaptureMode mode)
{
    const auto& names = CaptureModeNames();
    const auto  index = static_cast<std::size_t>(mode);
    return index < names.size() ? names[index] : std::wstring{};
}

std::wstring CaptureModeDescription(CaptureMode mode)
{
    switch (mode)
    {
    case CaptureMode::Realtime:
        return L"The changes of an installation are recorded while it runs: AppBox subscribes to the kernel events of "
               L"the file system and of the registry and merges what it observed once the installation is complete. "
               L"It needs Windows 10 or newer and an elevated process.";
    case CaptureMode::Snapshot:
        return L"The state of the machine is read before the installation and again after it, and the two reads are "
               L"compared: the difference of them is what the workspaces receive. It runs on every machine and needs "
               L"no elevation.";
    }

    return {};
}

std::vector<CaptureWizardStage> CaptureStages(CaptureMode mode)
{
    switch (mode)
    {
    case CaptureMode::Realtime:
        return { CaptureWizardStage::Mode, CaptureWizardStage::Recording, CaptureWizardStage::Review,
                 CaptureWizardStage::Done };
    case CaptureMode::Snapshot:
        return { CaptureWizardStage::Mode,     CaptureWizardStage::Snapshot,     CaptureWizardStage::SnapshotFile,
                 CaptureWizardStage::Baseline, CaptureWizardStage::Installation, CaptureWizardStage::Comparison,
                 CaptureWizardStage::Review,   CaptureWizardStage::Done };
    }

    return { CaptureWizardStage::Mode, CaptureWizardStage::Done };
}

CaptureWizardModel::CaptureWizardModel(const CaptureRequirements& requirements) : requirements_(requirements)
{
}

const CaptureRequirements& CaptureWizardModel::Requirements() const
{
    return requirements_;
}

CaptureMode CaptureWizardModel::Mode() const
{
    return mode_;
}

bool CaptureWizardModel::CanSelectMode(CaptureMode mode) const
{
    switch (mode)
    {
    case CaptureMode::Realtime:
        return requirements_.supported;
    case CaptureMode::Snapshot:
        return true;
    }

    return false;
}

bool CaptureWizardModel::SelectMode(CaptureMode mode)
{
    if (!CanSelectMode(mode))
    {
        return false;
    }

    if (mode == mode_)
    {
        return true;
    }

    /*
     * The mode is picked before the capture starts, so the mode which is left
     * behind never confirmed anything: a mode which is picked anew is a
     * capture which starts over.
     */
    mode_ = mode;
    installation_done_ = false;
    baseline_ready_ = false;
    snapshot_path_.clear();
    analysis_ready_ = false;
    fill_running_ = false;
    fill_complete_ = false;
    return true;
}

std::vector<CaptureWizardStage> CaptureWizardModel::Stages() const
{
    return CaptureStages(mode_);
}

CaptureWizardStage CaptureWizardModel::Stage() const
{
    return stage_;
}

std::size_t CaptureWizardModel::StageIndex(CaptureWizardStage stage) const
{
    const auto stages = Stages();
    const auto found = std::find(stages.begin(), stages.end(), stage);
    return found == stages.end() ? stages.size() : static_cast<std::size_t>(std::distance(stages.begin(), found));
}

CaptureStageState CaptureWizardModel::StateOf(CaptureWizardStage stage) const
{
    if (stage == stage_)
    {
        return CaptureStageState::Current;
    }

    return StageIndex(stage) < StageIndex(stage_) ? CaptureStageState::Done : CaptureStageState::Pending;
}

bool CaptureWizardModel::ModeReady() const
{
    switch (mode_)
    {
    case CaptureMode::Realtime:
        return CanSelectMode(mode_) && requirements_.elevated;
    case CaptureMode::Snapshot:
        return CanSelectMode(mode_);
    }

    return false;
}

void CaptureWizardModel::LeaveBaseline()
{
    if (stage_ == CaptureWizardStage::Baseline)
    {
        stage_ = CaptureWizardStage::SnapshotFile;
    }
}

bool CaptureWizardModel::CanAdvance() const
{
    switch (stage_)
    {
    case CaptureWizardStage::Mode:
        /* The mode has to be offered and the machine has to carry what it needs. */
        return ModeReady();
    case CaptureWizardStage::Snapshot:
        /* The source of the baseline is always there, so the step always lets the wizard go on. */
        return true;
    case CaptureWizardStage::SnapshotFile:
        /* The step only names the file the baseline is kept in. */
        return !snapshot_path_.empty();
    case CaptureWizardStage::Baseline:
        /*
         * The step waits for the run of the source: it is left once the
         * baseline is there, and a run which produced none sends the wizard
         * back to the step of the file.
         */
        return baseline_ready_;
    case CaptureWizardStage::Recording:
    case CaptureWizardStage::Installation:
        /* The user decides when the installation is over and confirms it. */
        return installation_done_;
    case CaptureWizardStage::Comparison:
        /* The comparison waits for the analysis of the changes. */
        return analysis_ready_;
    case CaptureWizardStage::Review:
        /*
         * The step of the analysis and the fill is left once the whole work of
         * the capture is over: the difference was analyzed, the entries were
         * repaired and the workspaces were written.
         */
        return fill_complete_;
    case CaptureWizardStage::Done:
        return false;
    }

    return false;
}

bool CaptureWizardModel::CanCancel() const
{
    /*
     * The workspaces are part of the session of the user, so the capture can be
     * dropped while its work is prepared and not while it is written into them
     * or after it was.
     */
    return !fill_running_ && !fill_complete_;
}

bool CaptureWizardModel::Advance()
{
    if (!CanAdvance())
    {
        return false;
    }

    const auto stages = Stages();
    const auto index = StageIndex(stage_);
    if (index + 1 >= stages.size())
    {
        return false;
    }

    stage_ = stages[index + 1];
    return true;
}

std::wstring CaptureWizardModel::AdvanceLabel() const
{
    switch (stage_)
    {
    case CaptureWizardStage::Review:
        return L"Confirm";
    case CaptureWizardStage::Done:
        return L"Close";
    case CaptureWizardStage::Mode:
    case CaptureWizardStage::Snapshot:
    case CaptureWizardStage::SnapshotFile:
    case CaptureWizardStage::Baseline:
    case CaptureWizardStage::Recording:
    case CaptureWizardStage::Installation:
    case CaptureWizardStage::Comparison:
        return L"Next";
    }

    return L"Next";
}

void CaptureWizardModel::SetInstallationDone(bool done)
{
    installation_done_ = done;
}

bool CaptureWizardModel::IsInstallationDone() const
{
    return installation_done_;
}

SnapshotBaselineSource CaptureWizardModel::BaselineSource() const
{
    return baseline_source_;
}

bool CaptureWizardModel::SelectBaselineSource(SnapshotBaselineSource source)
{
    if (mode_ != CaptureMode::Snapshot)
    {
        return false;
    }

    /*
     * The source which is picked anew names another work and another file, so
     * the path the user set and the baseline which was ready are dropped.
     */
    baseline_source_ = source;
    baseline_ready_ = false;
    snapshot_path_.clear();
    LeaveBaseline();
    return true;
}

const std::wstring& CaptureWizardModel::SnapshotPath() const
{
    return snapshot_path_;
}

void CaptureWizardModel::SetSnapshotPath(const std::wstring& path)
{
    if (path == snapshot_path_)
    {
        return;
    }

    snapshot_path_ = path;

    /*
     * The file which was named anew is not the one the state was read from or
     * written into, so the baseline has to be read again.
     */
    baseline_ready_ = false;
    LeaveBaseline();
}

void CaptureWizardModel::SetBaselineReady(bool ready)
{
    baseline_ready_ = ready;

    /*
     * A run which could not produce a baseline is fixed where the file is
     * named, so the step of the baseline sends the wizard back to it: the path
     * is still there and the button which leaves the step asks for the work
     * again.
     */
    if (!ready)
    {
        LeaveBaseline();
    }
}

bool CaptureWizardModel::IsBaselineReady() const
{
    return baseline_ready_;
}

void CaptureWizardModel::MarkAnalysisReady()
{
    analysis_ready_ = true;
}

bool CaptureWizardModel::IsAnalysisReady() const
{
    return analysis_ready_;
}

void CaptureWizardModel::SetFillRunning(bool running)
{
    fill_running_ = running;
}

bool CaptureWizardModel::IsFillRunning() const
{
    return fill_running_;
}

void CaptureWizardModel::MarkFillComplete()
{
    fill_running_ = false;
    fill_complete_ = true;
}

bool CaptureWizardModel::IsFillComplete() const
{
    return fill_complete_;
}

bool CaptureWizardModel::IsFinished() const
{
    return stage_ == CaptureWizardStage::Done;
}

} // namespace appbox
