// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <QString>

#include <string>

namespace WalletGui {

// Atomically installs an already-serialized encrypted wallet blob. The
// temporary file is created with an unpredictable exclusive name in the
// destination directory, the destination is never opened or truncated in
// place, and the staging file is restricted to the current user before any
// wallet bytes are written.
class AtomicWalletFile {
public:
  static bool write(const QString& destinationPath, const std::string& data,
                    QString& errorText);

  // Installs a previously written and validated private staging file. The
  // source is opened without following links and the open file is renamed,
  // rather than trusting the source pathname after validation.
  static bool replacePrivateFile(const QString& replacementPath,
                                 const QString& destinationPath,
                                 QString& errorText);

  // Public for focused platform regression tests and startup diagnostics.
  static bool isPrivateRegularFile(const QString& path, QString& errorText);
};

}  // namespace WalletGui
