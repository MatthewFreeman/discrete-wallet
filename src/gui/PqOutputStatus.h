// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QtGlobal>
#include <QString>

namespace WalletGui {

enum class PqOutputStatusTone {
  Unavailable,
  Ready,
  Attention,
  Pending
};

struct PqOutputStatusPresentation {
  QString countText;
  QString stateText;
  QString toolTip;
  PqOutputStatusTone tone = PqOutputStatusTone::Unavailable;
  quint64 estimatedOutputsAfterConfirmation = 0;
  bool consolidationRecommended = false;
};

PqOutputStatusPresentation makePqOutputStatusPresentation(
    bool _ready, quint64 _availableOutputs, quint64 _inputLimit,
    quint64 _selectedInputs, quint64 _resultingOutputs,
    const QString& _formattedFee, const QString& _ticker,
    bool _hasUnconfirmedTransaction);

}
