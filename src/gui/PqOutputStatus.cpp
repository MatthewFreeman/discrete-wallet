// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "PqOutputStatus.h"

#include <QCoreApplication>

namespace WalletGui {

namespace {

QString tr(const char* _text) {
  return QCoreApplication::translate("AccountFrame", _text);
}

}

PqOutputStatusPresentation makePqOutputStatusPresentation(
    bool _ready, quint64 _availableOutputs, quint64 _inputLimit,
    quint64 _selectedInputs, quint64 _resultingOutputs,
    const QString& _formattedFee, const QString& _ticker,
    bool _hasUnconfirmedTransaction) {
  PqOutputStatusPresentation presentation;
  presentation.countText = tr("-- / %1").arg(_inputLimit);

  if (!_ready) {
    presentation.stateText = tr("Available after synchronization");
    presentation.toolTip = tr(
        "The spendable PQ output count appears after the wallet is fully synchronized.");
    return presentation;
  }

  if (_hasUnconfirmedTransaction) {
    presentation.tone = PqOutputStatusTone::Pending;
    presentation.countText = tr("— / %1").arg(_inputLimit);
    presentation.stateText = tr("Recalculating after confirmation");
    presentation.toolTip = tr(
        "The spendable PQ output count is hidden while a wallet transaction is unconfirmed. "
        "Reserved inputs and pending wallet outputs make the intermediate number temporary. "
        "The count will refresh when the transaction confirms or leaves the pool.");
    return presentation;
  }

  presentation.countText =
      tr("%1 / %2").arg(_availableOutputs).arg(_inputLimit);
  const bool usefulPlan =
      _selectedInputs > 0 && _resultingOutputs < _selectedInputs;
  presentation.consolidationRecommended =
      _availableOutputs > _inputLimit && usefulPlan;
  presentation.estimatedOutputsAfterConfirmation = _availableOutputs;
  if (usefulPlan && _availableOutputs >= _selectedInputs) {
    presentation.estimatedOutputsAfterConfirmation =
        _availableOutputs - _selectedInputs + _resultingOutputs;
  }

  presentation.toolTip = tr(
      "This wallet has %1 spendable PQ outputs. A transaction can use at most %2 inputs.")
      .arg(_availableOutputs)
      .arg(_inputLimit);

  if (presentation.consolidationRecommended) {
    presentation.tone = PqOutputStatusTone::Attention;
    presentation.stateText = tr("Consolidation recommended");
    presentation.toolTip += tr(
        "\n\nNext consolidation: %1 inputs -> %2 outputs. "
        "Estimated after confirmation: %3 spendable outputs. Fee: %4 %5.")
        .arg(_selectedInputs)
        .arg(_resultingOutputs)
        .arg(presentation.estimatedOutputsAfterConfirmation)
        .arg(_formattedFee, _ticker);
    presentation.toolTip += tr(
        "\n\nPrivacy: consolidation publicly links the selected outputs as controlled by one wallet.");
    return presentation;
  }

  if (_availableOutputs > _inputLimit) {
    presentation.tone = PqOutputStatusTone::Attention;
    presentation.stateText = tr("Above limit; no reducing batch");
    presentation.toolTip += tr(
        "\n\nThe current output denominations cannot be combined into fewer canonical outputs after the fee. "
        "A large payment may still require a smaller amount.");
    return presentation;
  }

  presentation.tone = PqOutputStatusTone::Ready;
  presentation.stateText = tr("Within the transaction limit");
  presentation.toolTip += tr(
      "\n\nNo consolidation is currently needed for the output count.");
  return presentation;
}

}
