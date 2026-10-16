#include <gtest/gtest.h>
#include "src/core/CaptureWizardModel.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Build the prerequisites of a machine which can record.
 * @return The prerequisites.
 */
appbox::CaptureRequirements ReadyRequirements()
{
    appbox::CaptureRequirements requirements;
    requirements.supported = true;
    requirements.elevated = true;
    return requirements;
}

} // namespace

/**
 * @brief A fresh wizard shows the choice of the mode, with every other step
 *        still ahead of it.
 */
TEST(Unit_CaptureWizardModel, StartsOnTheModeStep)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Mode);
    EXPECT_EQ(model.Mode(), appbox::CaptureMode::Realtime);
    EXPECT_EQ(model.Stages(), (std::vector<appbox::CaptureWizardStage>{
                                  appbox::CaptureWizardStage::Mode, appbox::CaptureWizardStage::Recording,
                                  appbox::CaptureWizardStage::Review, appbox::CaptureWizardStage::Done }));
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Mode), appbox::CaptureStageState::Current);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Recording), appbox::CaptureStageState::Pending);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Review), appbox::CaptureStageState::Pending);
    EXPECT_FALSE(model.IsAnalysisReady());
    EXPECT_FALSE(model.IsFinished());
    EXPECT_TRUE(model.CanAdvance());
    EXPECT_EQ(model.AdvanceLabel(), L"Next");
}

/**
 * @brief A machine which carries the kernel events but runs without elevation
 *        offers the mode and refuses to record until the process is restarted
 *        as an administrator.
 */
TEST(Unit_CaptureWizardModel, WaitsForTheElevation)
{
    appbox::CaptureRequirements requirements;
    requirements.supported = true;
    requirements.elevated = false;

    appbox::CaptureWizardModel model(requirements);

    EXPECT_FALSE(requirements.Met());
    EXPECT_TRUE(model.CanSelectMode(appbox::CaptureMode::Realtime));
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Mode);
}

/**
 * @brief A machine which does not carry the kernel events cannot pick the
 *        realtime capture at all, so the first step never lets the wizard go on
 *        while it is picked.
 */
TEST(Unit_CaptureWizardModel, RefusesAModeTheMachineCannotRun)
{
    appbox::CaptureRequirements requirements;
    requirements.supported = false;
    requirements.elevated = true;

    appbox::CaptureWizardModel model(requirements);

    EXPECT_FALSE(model.CanSelectMode(appbox::CaptureMode::Realtime));
    EXPECT_FALSE(model.SelectMode(appbox::CaptureMode::Realtime));
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Mode);
}

/**
 * @brief The snapshot capture reads the machine instead of observing it, so it
 *        is offered and can be walked on a machine which carries neither the
 *        kernel events nor an elevated process.
 */
TEST(Unit_CaptureWizardModel, OffersTheSnapshotCaptureOnEveryMachine)
{
    appbox::CaptureRequirements requirements;
    requirements.supported = false;
    requirements.elevated = false;

    appbox::CaptureWizardModel model(requirements);

    EXPECT_TRUE(model.CanSelectMode(appbox::CaptureMode::Snapshot));
    ASSERT_TRUE(model.SelectMode(appbox::CaptureMode::Snapshot));
    EXPECT_EQ(model.Mode(), appbox::CaptureMode::Snapshot);
    EXPECT_TRUE(model.CanAdvance());
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Snapshot);
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);

    /* The step of the file waits for the file the user names, on every machine. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    model.SetSnapshotPath(L"C:\\capture\\baseline.snapshot");
    EXPECT_TRUE(model.CanAdvance());
}

/**
 * @brief The steps of a realtime capture follow each other, and the step of
 *        the review waits for the analysis of the recording.
 */
TEST(Unit_CaptureWizardModel, WalksTheStepsOfARealtimeCapture)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Recording);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Mode), appbox::CaptureStageState::Done);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Recording), appbox::CaptureStageState::Current);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Review), appbox::CaptureStageState::Pending);
    EXPECT_EQ(model.AdvanceLabel(), L"Next");

    /* The recording ends on the decision of the user, which the step waits for. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Recording);

    model.SetInstallationDone(true);
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Review);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Recording), appbox::CaptureStageState::Done);
    EXPECT_EQ(model.AdvanceLabel(), L"Confirm");

    /* The last step waits for the whole work of the capture. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Review);

    model.MarkFillComplete();
    EXPECT_TRUE(model.IsFillComplete());
    ASSERT_TRUE(model.Advance());

    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Done);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Review), appbox::CaptureStageState::Done);
    EXPECT_TRUE(model.IsFinished());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
}

/**
 * @brief The snapshot capture walks the steps of its own: the source of the
 *        baseline, the file of the baseline, the baseline, the installation and
 *        the comparison come before the review, and the recording of the
 *        realtime capture is not part of its walk.
 */
TEST(Unit_CaptureWizardModel, WalksTheStepsOfASnapshotCapture)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    ASSERT_TRUE(model.SelectMode(appbox::CaptureMode::Snapshot));
    EXPECT_EQ(model.Stages(), (std::vector<appbox::CaptureWizardStage>{
                                  appbox::CaptureWizardStage::Mode, appbox::CaptureWizardStage::Snapshot,
                                  appbox::CaptureWizardStage::SnapshotFile, appbox::CaptureWizardStage::Baseline,
                                  appbox::CaptureWizardStage::Installation, appbox::CaptureWizardStage::Comparison,
                                  appbox::CaptureWizardStage::Review, appbox::CaptureWizardStage::Done }));

    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Snapshot);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Mode), appbox::CaptureStageState::Done);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Snapshot), appbox::CaptureStageState::Current);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::SnapshotFile), appbox::CaptureStageState::Pending);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Baseline), appbox::CaptureStageState::Pending);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Installation), appbox::CaptureStageState::Pending);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Recording), appbox::CaptureStageState::Pending);

    /* The source of the baseline is always there, so the step always lets the wizard go on. */
    EXPECT_TRUE(model.CanAdvance());
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Snapshot), appbox::CaptureStageState::Done);
    EXPECT_EQ(model.AdvanceLabel(), L"Next");

    /* The step of the file only names the file the baseline is kept in. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);

    model.SetSnapshotPath(L"C:\\capture\\baseline.snapshot");
    EXPECT_EQ(model.SnapshotPath(), L"C:\\capture\\baseline.snapshot");
    EXPECT_TRUE(model.CanAdvance());

    /* The step which follows waits for the run of the source. */
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Baseline);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::SnapshotFile), appbox::CaptureStageState::Done);
    EXPECT_FALSE(model.IsBaselineReady());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Baseline);
    EXPECT_EQ(model.AdvanceLabel(), L"Next");

    /* A run which produced no baseline shows the step which names the file again. */
    model.SetBaselineReady(false);
    EXPECT_FALSE(model.IsBaselineReady());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);
    EXPECT_TRUE(model.CanAdvance());

    /* The run which is asked for again reports the baseline, and the wizard walks on. */
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Baseline);
    model.SetBaselineReady(true);
    EXPECT_TRUE(model.IsBaselineReady());
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Installation);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Baseline), appbox::CaptureStageState::Done);
    EXPECT_EQ(model.AdvanceLabel(), L"Next");

    /* The installation ends on the decision of the user. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    model.SetInstallationDone(true);
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Comparison);
    EXPECT_EQ(model.StateOf(appbox::CaptureWizardStage::Installation), appbox::CaptureStageState::Done);

    /* The comparison waits for the analysis of the difference. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Comparison);

    model.MarkAnalysisReady();
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Review);
    EXPECT_EQ(model.AdvanceLabel(), L"Confirm");

    /* The last step waits for the workspaces as well. */
    EXPECT_FALSE(model.CanAdvance());
    model.MarkFillComplete();
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Done);
    EXPECT_TRUE(model.IsFinished());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
}

/**
 * @brief The last step is left once the workspaces are written, and the capture
 *        cannot be dropped while they are written.
 */
TEST(Unit_CaptureWizardModel, WaitsForTheWorkspacesBeforeTheCaptureEnds)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    /* The capture can be dropped while its work is prepared. */
    EXPECT_TRUE(model.CanCancel());

    ASSERT_TRUE(model.Advance());
    model.SetInstallationDone(true);
    ASSERT_TRUE(model.Advance());
    ASSERT_EQ(model.Stage(), appbox::CaptureWizardStage::Review);

    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.IsFillRunning());
    EXPECT_FALSE(model.IsFillComplete());
    EXPECT_TRUE(model.CanCancel());

    /* The workspaces are written, which is the phase the user cannot stop. */
    model.SetFillRunning(true);
    EXPECT_TRUE(model.IsFillRunning());
    EXPECT_FALSE(model.CanCancel());
    EXPECT_FALSE(model.CanAdvance());

    model.MarkFillComplete();
    EXPECT_TRUE(model.IsFillComplete());
    EXPECT_FALSE(model.IsFillRunning());
    EXPECT_FALSE(model.CanCancel()) << "a capture which wrote the workspaces is over";
    EXPECT_TRUE(model.CanAdvance());
    ASSERT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Done);
}

/**
 * @brief The step of the recording waits for the user to confirm that the
 *        installation is over, and the confirmation can be taken back.
 */
TEST(Unit_CaptureWizardModel, WaitsForTheConfirmationOfTheInstallation)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    ASSERT_TRUE(model.Advance());
    ASSERT_EQ(model.Stage(), appbox::CaptureWizardStage::Recording);
    EXPECT_FALSE(model.IsInstallationDone());

    /* The step does not let the wizard go on before the user confirmed it. */
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Recording);

    model.SetInstallationDone(true);
    EXPECT_TRUE(model.IsInstallationDone());
    EXPECT_TRUE(model.CanAdvance());

    /* A confirmation which is taken back closes the step again. */
    model.SetInstallationDone(false);
    EXPECT_FALSE(model.IsInstallationDone());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Recording);

    model.SetInstallationDone(true);
    EXPECT_TRUE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::Review);
}

/**
 * @brief The step of the file of the baseline waits for the file the state is
 *        read from or written into, the step of the baseline waits for the run
 *        of the source, and the source of the baseline can be picked again.
 */
TEST(Unit_CaptureWizardModel, WaitsForTheBaselineOfTheSnapshotCapture)
{
    appbox::CaptureWizardModel model(ReadyRequirements());

    /* The mode which reads no baseline cannot pick a source for one. */
    EXPECT_EQ(model.BaselineSource(), appbox::SnapshotBaselineSource::New);
    EXPECT_FALSE(model.SelectBaselineSource(appbox::SnapshotBaselineSource::Existing));

    ASSERT_TRUE(model.SelectMode(appbox::CaptureMode::Snapshot));
    ASSERT_TRUE(model.Advance());
    ASSERT_EQ(model.Stage(), appbox::CaptureWizardStage::Snapshot);
    EXPECT_FALSE(model.IsBaselineReady());

    ASSERT_TRUE(model.SelectBaselineSource(appbox::SnapshotBaselineSource::Existing));
    EXPECT_EQ(model.BaselineSource(), appbox::SnapshotBaselineSource::Existing);
    ASSERT_TRUE(model.Advance());
    ASSERT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);

    /* The step waits for the file the baseline is kept in. */
    EXPECT_TRUE(model.SnapshotPath().empty());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());

    model.SetSnapshotPath(L"C:\\capture\\baseline.snapshot");
    EXPECT_TRUE(model.CanAdvance());
    ASSERT_TRUE(model.Advance());
    ASSERT_EQ(model.Stage(), appbox::CaptureWizardStage::Baseline);

    model.SetBaselineReady(true);
    EXPECT_TRUE(model.IsBaselineReady());
    EXPECT_TRUE(model.CanAdvance());

    /* A path which is set anew names another file, so the baseline is read again. */
    model.SetSnapshotPath(L"C:\\capture\\other.snapshot");
    EXPECT_FALSE(model.IsBaselineReady());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);
    EXPECT_TRUE(model.CanAdvance());

    /* Picking a source anew drops the file of the source which was left behind. */
    ASSERT_TRUE(model.SelectBaselineSource(appbox::SnapshotBaselineSource::New));
    EXPECT_EQ(model.BaselineSource(), appbox::SnapshotBaselineSource::New);
    EXPECT_TRUE(model.SnapshotPath().empty());
    EXPECT_FALSE(model.IsBaselineReady());
    EXPECT_FALSE(model.CanAdvance());
    EXPECT_FALSE(model.Advance());
    EXPECT_EQ(model.Stage(), appbox::CaptureWizardStage::SnapshotFile);
}

/**
 * @brief The prerequisites are described the way the first step shows them,
 *        and a missing one names what the user can do about it.
 */
TEST(Unit_CaptureWizardModel, DescribesTheRequirements)
{
    const auto ready = appbox::DescribeRequirements(ReadyRequirements());
    ASSERT_EQ(ready.size(), 2u);
    EXPECT_EQ(ready[0].name, L"Windows 10 or newer");
    EXPECT_TRUE(ready[0].met);
    EXPECT_EQ(ready[1].name, L"Elevated process");
    EXPECT_TRUE(ready[1].met);

    appbox::CaptureRequirements missing;
    missing.supported = false;
    missing.elevated = false;

    const auto blocked = appbox::DescribeRequirements(missing);
    ASSERT_EQ(blocked.size(), 2u);
    EXPECT_FALSE(blocked[0].met);
    EXPECT_NE(blocked[0].detail.find(L"snapshot capture"), std::wstring::npos);
    EXPECT_FALSE(blocked[1].met);
    EXPECT_NE(blocked[1].detail.find(L"administrator"), std::wstring::npos);
}

/**
 * @brief The capture modes are named and explained the way the first step
 *        offers them, so no dialog writes the text of a mode of its own.
 */
TEST(Unit_CaptureWizardModel, DescribesTheCaptureModes)
{
    const auto& names = appbox::CaptureModeNames();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], L"Realtime capture");
    EXPECT_EQ(names[1], L"Snapshot capture");
    EXPECT_EQ(appbox::CaptureModeName(appbox::CaptureMode::Realtime), names[0]);
    EXPECT_EQ(appbox::CaptureModeName(appbox::CaptureMode::Snapshot), names[1]);

    /* The description of a mode names what it does and what it asks the machine for. */
    const auto realtime = appbox::CaptureModeDescription(appbox::CaptureMode::Realtime);
    EXPECT_NE(realtime.find(L"kernel events"), std::wstring::npos);
    EXPECT_NE(realtime.find(L"elevated"), std::wstring::npos);

    const auto snapshot = appbox::CaptureModeDescription(appbox::CaptureMode::Snapshot);
    EXPECT_NE(snapshot.find(L"compared"), std::wstring::npos);
    EXPECT_EQ(snapshot.find(L"kernel events"), std::wstring::npos);
}
