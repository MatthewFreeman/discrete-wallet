// Copyright (c) 2026, The Discrete developers
// SPDX-License-Identifier: LGPL-3.0-or-later

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "security/AtomicWalletFile.h"

namespace {

bool require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

QByteArray readAll(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

bool writeBytes(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() &&
      file.flush();
}

bool hasStagingFiles(const QString& directoryPath) {
  return !QDir(directoryPath).entryList(
      QStringList{QStringLiteral(".discrete-wallet-save-*.tmp")},
      QDir::Files | QDir::Hidden | QDir::NoSymLinks)
              .isEmpty();
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  if (!require(directory.isValid(), "temporary directory creation failed")) {
    return 1;
  }

  QString error;
  const QString walletPath = directory.filePath(QStringLiteral("primary.wallet"));
  const QString legacyTempPath = walletPath + QStringLiteral(".temp");
  if (!require(writeBytes(walletPath, QByteArrayLiteral("old-wallet")) &&
                   writeBytes(legacyTempPath, QByteArrayLiteral("attacker-owned")),
               "baseline files could not be created")) {
    return 1;
  }
  if (!WalletGui::AtomicWalletFile::write(
          walletPath, std::string("encrypted-wallet"), error)) {
    std::cerr << "atomic write failed: " << error.toStdString() << '\n';
    return 1;
  }
  if (!require(readAll(walletPath) == QByteArrayLiteral("encrypted-wallet"),
               "atomic write did not install the new wallet") ||
      !require(readAll(legacyTempPath) == QByteArrayLiteral("attacker-owned"),
               "the predictable legacy .temp path was reused") ||
      !require(!hasStagingFiles(directory.path()),
               "successful commit left a staging file")) {
    return 1;
  }
  if (!WalletGui::AtomicWalletFile::isPrivateRegularFile(walletPath, error)) {
    std::cerr << "private-file verification failed: "
              << error.toStdString() << '\n';
    return 1;
  }
#ifdef Q_OS_WIN
  const DWORD walletAttributes = ::GetFileAttributesW(
      reinterpret_cast<LPCWSTR>(walletPath.utf16()));
  if (!require(walletAttributes != INVALID_FILE_ATTRIBUTES &&
                   (walletAttributes &
                    (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY)) == 0,
               "staging-only Windows attributes leaked onto the wallet")) {
    return 1;
  }
  const QString alternateStreamPath =
      walletPath + QStringLiteral(":attacker-stream");
  if (!require(!WalletGui::AtomicWalletFile::write(
                   alternateStreamPath, std::string("must-not-write"), error),
               "Windows alternate-data-stream destination was accepted") ||
      !require(!hasStagingFiles(directory.path()),
               "rejected alternate-data-stream path left a staging file")) {
    return 1;
  }
#endif

  const QString validatedStage =
      directory.filePath(QStringLiteral("validated-stage.wallet"));
  const QString installedPath =
      directory.filePath(QStringLiteral("installed.wallet"));
  if (!WalletGui::AtomicWalletFile::write(
          validatedStage, std::string("validated-protected-wallet"), error)) {
    std::cerr << "validated stage write failed: " << error.toStdString() << '\n';
    return 1;
  }
  if (!WalletGui::AtomicWalletFile::write(
          installedPath, std::string("old-full-wallet"), error)) {
    std::cerr << "replacement baseline write failed: " << error.toStdString() << '\n';
    return 1;
  }
  if (!WalletGui::AtomicWalletFile::replacePrivateFile(
          validatedStage, installedPath, error)) {
    std::cerr << "private replacement failed: " << error.toStdString() << '\n';
    return 1;
  }
  if (
      !require(!QFile::exists(validatedStage) &&
                   readAll(installedPath) ==
                       QByteArrayLiteral("validated-protected-wallet"),
               "validated private replacement was not installed") ||
      !require(!hasStagingFiles(directory.path()),
               "private replacement left a staging file")) {
    return 1;
  }

  // A validated staging pathname must still resolve to the same single-link
  // file at commit time. Adding a hard link simulates a replacement/tampering
  // attempt between validation and installation.
  const QString linkedStage =
      directory.filePath(QStringLiteral("linked-stage.wallet"));
  const QString linkedStageAlias =
      directory.filePath(QStringLiteral("linked-stage-alias.wallet"));
  const QString guardedDestination =
      directory.filePath(QStringLiteral("guarded-destination.wallet"));
  if (!require(WalletGui::AtomicWalletFile::write(
                   linkedStage, std::string("candidate"), error),
               qPrintable(error)) ||
      !require(WalletGui::AtomicWalletFile::write(
                   guardedDestination, std::string("keep-this"), error),
               qPrintable(error))) {
    return 1;
  }
#ifdef Q_OS_WIN
  const bool stageLinked = ::CreateHardLinkW(
      reinterpret_cast<LPCWSTR>(linkedStageAlias.utf16()),
      reinterpret_cast<LPCWSTR>(linkedStage.utf16()), nullptr) != 0;
#else
  const QByteArray linkedStageName = QFile::encodeName(linkedStage);
  const QByteArray linkedStageAliasName = QFile::encodeName(linkedStageAlias);
  const bool stageLinked = ::link(linkedStageName.constData(),
                                  linkedStageAliasName.constData()) == 0;
#endif
  if (!require(stageLinked, "staging hard link could not be created") ||
      !require(!WalletGui::AtomicWalletFile::replacePrivateFile(
                   linkedStage, guardedDestination, error),
               "multi-link staging file was accepted") ||
      !require(readAll(guardedDestination) == QByteArrayLiteral("keep-this") &&
                   QFile::exists(linkedStage) && QFile::exists(linkedStageAlias),
               "rejected staging tamper changed filesystem state")) {
    return 1;
  }

  // Replacing one name of a hard-linked file must not overwrite the other
  // name's contents. A truncate-in-place staging design fails this property.
  const QString protectedPath =
      directory.filePath(QStringLiteral("protected.wallet"));
  const QString destinationPath =
      directory.filePath(QStringLiteral("hardlink.wallet"));
  if (!require(writeBytes(protectedPath, QByteArrayLiteral("do-not-touch")),
               "hard-link target could not be created")) {
    return 1;
  }
#ifdef Q_OS_WIN
  const bool linked = ::CreateHardLinkW(
      reinterpret_cast<LPCWSTR>(destinationPath.utf16()),
      reinterpret_cast<LPCWSTR>(protectedPath.utf16()), nullptr) != 0;
#else
  const QByteArray destinationName = QFile::encodeName(destinationPath);
  const QByteArray protectedName = QFile::encodeName(protectedPath);
  const bool linked = ::link(protectedName.constData(),
                             destinationName.constData()) == 0;
#endif
  if (!require(linked, "hard link could not be created") ||
      !require(WalletGui::AtomicWalletFile::write(
                   destinationPath, std::string("replacement"), error),
               qPrintable(error)) ||
      !require(readAll(protectedPath) == QByteArrayLiteral("do-not-touch"),
               "atomic replacement modified the hard-link target") ||
      !require(readAll(destinationPath) == QByteArrayLiteral("replacement"),
               "atomic replacement did not replace the hard-link name") ||
      !require(!hasStagingFiles(directory.path()),
               "hard-link replacement left a staging file")) {
    return 1;
  }

  // A failed commit must preserve the existing filesystem object and must not
  // leave a predictable staging file.
  const QString directoryDestination =
      directory.filePath(QStringLiteral("cannot-replace.wallet"));
  if (!require(QDir().mkdir(directoryDestination),
               "directory destination could not be created") ||
      !require(!WalletGui::AtomicWalletFile::write(
                   directoryDestination, std::string("must-fail"), error),
               "atomic write unexpectedly replaced a directory") ||
      !require(QFileInfo(directoryDestination).isDir(),
               "failed commit damaged the directory destination") ||
      !require(!QFile::exists(directoryDestination + QStringLiteral(".temp")),
               "failed commit left a predictable staging file") ||
      !require(!hasStagingFiles(directory.path()),
               "failed commit left a randomized staging file")) {
    return 1;
  }

#ifndef Q_OS_WIN
  // A destination symlink is replaced as a directory entry; the link target
  // must never be opened or modified.
  const QString symlinkTarget =
      directory.filePath(QStringLiteral("symlink-target.wallet"));
  const QString symlinkDestination =
      directory.filePath(QStringLiteral("symlink-destination.wallet"));
  const QByteArray encodedTarget = QFile::encodeName(symlinkTarget);
  const QByteArray encodedDestination = QFile::encodeName(symlinkDestination);
  if (!require(writeBytes(symlinkTarget, QByteArrayLiteral("do-not-follow")),
               "symlink target could not be created") ||
      !require(::symlink(encodedTarget.constData(),
                         encodedDestination.constData()) == 0,
               "destination symlink could not be created") ||
      !require(WalletGui::AtomicWalletFile::write(
                   symlinkDestination, std::string("replacement"), error),
               qPrintable(error)) ||
      !require(readAll(symlinkTarget) == QByteArrayLiteral("do-not-follow") &&
                   readAll(symlinkDestination) == QByteArrayLiteral("replacement"),
               "atomic write followed the destination symlink")) {
    return 1;
  }

  const QString unsafeDirectory =
      directory.filePath(QStringLiteral("unsafe-parent"));
  if (!require(QDir().mkdir(unsafeDirectory),
               "unsafe parent could not be created")) {
    return 1;
  }
  const QByteArray encodedUnsafe = QFile::encodeName(unsafeDirectory);
  if (!require(::chmod(encodedUnsafe.constData(), 0777) == 0,
               "unsafe parent permissions could not be set") ||
      !require(!WalletGui::AtomicWalletFile::write(
                   QDir(unsafeDirectory).filePath(QStringLiteral("wallet")),
                   std::string("must-fail"), error),
               "write unexpectedly accepted a group/world-writable parent")) {
    ::chmod(encodedUnsafe.constData(), 0700);
    return 1;
  }
  ::chmod(encodedUnsafe.constData(), 0700);
#endif

  std::cout << "AtomicWalletFileTests: all checks passed\n";
  return 0;
}
