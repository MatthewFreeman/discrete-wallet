#include <cstdlib>
#include <iostream>

#include <QCoreApplication>

#include "gui/PqOutputStatus.h"

namespace {

void require(bool _condition, const char* _message) {
  if (!_condition) {
    std::cerr << _message << std::endl;
    std::exit(1);
  }
}

}

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);

  using namespace WalletGui;

  const auto unavailable = makePqOutputStatusPresentation(
      false, 0, 32, 0, 0, QStringLiteral("0.01"),
      QStringLiteral("XDS"), false);
  require(unavailable.tone == PqOutputStatusTone::Unavailable,
          "unsynchronized status must be unavailable");
  require(unavailable.countText == QStringLiteral("-- / 32"),
          "unsynchronized count must retain the input limit");

  const auto atLimit = makePqOutputStatusPresentation(
      true, 32, 32, 32, 5, QStringLiteral("0.01"),
      QStringLiteral("XDS"), false);
  require(atLimit.tone == PqOutputStatusTone::Ready,
          "exactly 32 outputs must remain within the limit");
  require(atLimit.stateText == QStringLiteral("No output maintenance needed"),
          "healthy state must avoid implying a wallet-level 32-output limit");
  require(!atLimit.consolidationRecommended,
          "exactly 32 outputs must not recommend consolidation");

  const auto recommended = makePqOutputStatusPresentation(
      true, 38, 32, 32, 5, QStringLiteral("0.01"),
      QStringLiteral("XDS"), false);
  require(recommended.tone == PqOutputStatusTone::Attention,
          "more than 32 outputs with a useful plan must need attention");
  require(recommended.consolidationRecommended,
          "useful over-limit plan must recommend consolidation");
  require(recommended.estimatedOutputsAfterConfirmation == 11,
          "estimated post-confirmation output count is wrong");
  require(recommended.toolTip.contains(QStringLiteral("32 input(s) -> 5 output(s)")),
          "tooltip must expose the exact consolidation plan");
  require(recommended.toolTip.contains(QStringLiteral("11 spendable outputs")),
          "tooltip must expose the estimated post-confirmation count");
  require(recommended.toolTip.contains(QStringLiteral("Fee: 0.01 XDS")),
          "tooltip must expose the fee");
  require(recommended.toolTip.contains(QStringLiteral("Privacy")),
          "tooltip must expose the privacy cost");

  const auto oversized = makePqOutputStatusPresentation(
      true, 1, 32, 1, 2, QStringLiteral("0.01"),
      QStringLiteral("XDS"), false);
  require(oversized.tone == PqOutputStatusTone::Attention,
          "one oversized note must require maintenance below the input limit");
  require(oversized.consolidationRecommended,
          "oversized note must be actionable");
  require(oversized.stateText == QStringLiteral("Output above 10,000 XDS"),
          "oversized note must not claim count reduction");
  require(oversized.estimatedOutputsAfterConfirmation == 2,
          "split must expose the increased output count");

  const auto pending = makePqOutputStatusPresentation(
      true, 6, 32, 6, 2, QStringLiteral("0.01"),
      QStringLiteral("XDS"), true);
  require(pending.tone == PqOutputStatusTone::Pending,
          "unconfirmed wallet transaction must show pending state");
  require(pending.countText == QStringLiteral("— / 32"),
          "pending state must hide the temporary output count");
  require(pending.stateText ==
              QStringLiteral("Recalculating after confirmation"),
          "pending state text is wrong");
  require(pending.toolTip.contains(QStringLiteral("count is hidden")),
          "pending tooltip must explain why the count is hidden");
  require(!pending.consolidationRecommended,
          "pending state must not recommend another consolidation");

  const auto noUsefulPlan = makePqOutputStatusPresentation(
      true, 40, 32, 32, 32, QStringLiteral("0.01"),
      QStringLiteral("XDS"), false);
  require(noUsefulPlan.tone == PqOutputStatusTone::Attention,
          "an over-limit non-reducing plan must remain visible as a warning");
  require(!noUsefulPlan.consolidationRecommended,
          "a non-reducing plan must not recommend consolidation");
  require(noUsefulPlan.stateText ==
              QStringLiteral("Above limit; no reducing batch"),
          "non-reducing over-limit state text is wrong");

  require(canRunManualMaintenance(true, true, false, false),
          "a useful synchronized plan must allow one manual step");
  require(!canRunManualMaintenance(true, true, true, false),
          "a pending transaction must block a second manual step");
  require(!canRunManualMaintenance(true, true, false, true),
          "an active maintenance attempt must block a second manual step");
  require(!canRunManualMaintenance(true, false, false, false),
          "a non-useful plan must disable the manual action");
  require(!canRunManualMaintenance(false, true, false, false),
          "an unsynchronized wallet must disable the manual action");

  return 0;
}
