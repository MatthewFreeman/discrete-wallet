// Copyright (c) 2026 The Discrete developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "AtomicWalletFile.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QDir>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

#ifdef Q_OS_WIN
#include <Aclapi.h>
#include <qt_windows.h>
#include <winternl.h>

#include <cstring>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace WalletGui {
namespace {

#ifdef Q_OS_WIN

struct LocalFreeDeleter {
  void operator()(void* pointer) const {
    if (pointer != nullptr) {
      ::LocalFree(pointer);
    }
  }
};

using LocalPointer = std::unique_ptr<void, LocalFreeDeleter>;

bool currentUserSid(std::vector<unsigned char>& tokenBuffer, PSID& sid,
                    QString& errorText) {
  HANDLE token = nullptr;
  if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
    errorText = QStringLiteral("OpenProcessToken failed (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }

  DWORD required = 0;
  ::GetTokenInformation(token, TokenUser, nullptr, 0, &required);
  const DWORD sizeError = ::GetLastError();
  if (required == 0 || sizeError != ERROR_INSUFFICIENT_BUFFER) {
    ::CloseHandle(token);
    errorText = QStringLiteral("GetTokenInformation sizing failed (Windows error %1)")
                    .arg(sizeError);
    return false;
  }

  tokenBuffer.resize(required);
  if (!::GetTokenInformation(token, TokenUser, tokenBuffer.data(), required,
                             &required)) {
    const DWORD error = ::GetLastError();
    ::CloseHandle(token);
    errorText = QStringLiteral("GetTokenInformation failed (Windows error %1)")
                    .arg(error);
    return false;
  }
  ::CloseHandle(token);

  sid = reinterpret_cast<TOKEN_USER*>(tokenBuffer.data())->User.Sid;
  if (!::IsValidSid(sid)) {
    errorText = QStringLiteral("the current Windows user SID is invalid");
    return false;
  }
  return true;
}

bool privateAcl(PACL& acl, QString& errorText) {
  std::vector<unsigned char> tokenBuffer;
  PSID currentSid = nullptr;
  if (!currentUserSid(tokenBuffer, currentSid, errorText)) {
    return false;
  }

  EXPLICIT_ACCESSW access{};
  access.grfAccessPermissions = FILE_ALL_ACCESS;
  access.grfAccessMode = SET_ACCESS;
  access.grfInheritance = NO_INHERITANCE;
  access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  access.Trustee.TrusteeType = TRUSTEE_IS_USER;
  access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(currentSid);

  const DWORD aclError = ::SetEntriesInAclW(1, &access, nullptr, &acl);
  if (aclError != ERROR_SUCCESS) {
    errorText = QStringLiteral("SetEntriesInAcl failed (Windows error %1)")
                    .arg(aclError);
    return false;
  }

  return true;
}

std::wstring longWindowsPath(const QString& path) {
  QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  if (native.startsWith(QStringLiteral("\\\\?\\"))) {
    return native.toStdWString();
  }
  if (native.startsWith(QStringLiteral("\\\\"))) {
    native = QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
  } else {
    native.prepend(QStringLiteral("\\\\?\\"));
  }
  return native.toStdWString();
}

bool verifyHandle(HANDLE handle, QString& errorText) {
  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(handle, &info)) {
    errorText = QStringLiteral("GetFileInformationByHandle failed (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }
  if ((info.dwFileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
    errorText = QStringLiteral("wallet destination is not a regular file");
    return false;
  }
  if (info.nNumberOfLinks != 1) {
    errorText = QStringLiteral("wallet destination has multiple hard links");
    return false;
  }

  std::vector<unsigned char> tokenBuffer;
  PSID currentSid = nullptr;
  if (!currentUserSid(tokenBuffer, currentSid, errorText)) {
    return false;
  }

  PSID ownerSid = nullptr;
  PACL dacl = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD securityError = ::GetSecurityInfo(
      handle, SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
      &ownerSid, nullptr, &dacl, nullptr, &descriptor);
  LocalPointer descriptorOwner(descriptor);
  if (securityError != ERROR_SUCCESS) {
    errorText = QStringLiteral("GetSecurityInfo failed (Windows error %1)")
                    .arg(securityError);
    return false;
  }
  if (ownerSid == nullptr || !::EqualSid(ownerSid, currentSid) || dacl == nullptr) {
    errorText = QStringLiteral("wallet file is not owned and protected by the current user");
    return false;
  }

  SECURITY_DESCRIPTOR_CONTROL control = 0;
  DWORD revision = 0;
  if (!::GetSecurityDescriptorControl(descriptor, &control, &revision) ||
      (control & SE_DACL_PROTECTED) == 0) {
    errorText = QStringLiteral("wallet file DACL still inherits from its directory");
    return false;
  }

  ACL_SIZE_INFORMATION aclInfo{};
  if (!::GetAclInformation(dacl, &aclInfo, sizeof(aclInfo),
                           AclSizeInformation)) {
    errorText = QStringLiteral("GetAclInformation failed (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }
  bool currentUserAllowed = false;
  for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
    void* rawAce = nullptr;
    if (!::GetAce(dacl, i, &rawAce)) {
      errorText = QStringLiteral("GetAce failed (Windows error %1)")
                      .arg(::GetLastError());
      return false;
    }
    const ACE_HEADER* header = static_cast<const ACE_HEADER*>(rawAce);
    if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
      continue;
    }
    const ACCESS_ALLOWED_ACE* ace =
        static_cast<const ACCESS_ALLOWED_ACE*>(rawAce);
    PSID aceSid = const_cast<DWORD*>(&ace->SidStart);
    if (!::EqualSid(aceSid, currentSid)) {
      errorText = QStringLiteral("wallet file grants access to another principal");
      return false;
    }
    currentUserAllowed = true;
  }
  if (!currentUserAllowed) {
    errorText = QStringLiteral("wallet file does not grant access to the current user");
    return false;
  }
  return true;
}

HANDLE openDirectoryHandle(const QString& directoryPath,
                           QString& errorText) {
  const std::wstring nativeDirectory = longWindowsPath(directoryPath);
  HANDLE directory = ::CreateFileW(
      nativeDirectory.c_str(),
      FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_ADD_FILE | FILE_DELETE_CHILD |
          READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (directory == INVALID_HANDLE_VALUE) {
    errorText = QStringLiteral("cannot open the wallet directory securely (Windows error %1)")
                    .arg(::GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  BY_HANDLE_FILE_INFORMATION info{};
  if (!::GetFileInformationByHandle(directory, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    const DWORD error = ::GetLastError();
    ::CloseHandle(directory);
    errorText = error == ERROR_SUCCESS
        ? QStringLiteral("wallet parent is not a non-reparse directory")
        : QStringLiteral("cannot inspect the wallet directory (Windows error %1)")
              .arg(error);
    return INVALID_HANDLE_VALUE;
  }
  return directory;
}

bool readbackMatches(HANDLE handle, const std::string& data,
                     QString& errorText) {
  LARGE_INTEGER size{};
  if (!::GetFileSizeEx(handle, &size) || size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) != data.size()) {
    errorText = QStringLiteral("wallet staging readback size mismatch");
    return false;
  }
  LARGE_INTEGER start{};
  if (!::SetFilePointerEx(handle, start, nullptr, FILE_BEGIN)) {
    errorText = QStringLiteral("wallet staging seek failed (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }

  std::vector<char> buffer(64 * 1024);
  std::size_t offset = 0;
  while (offset < data.size()) {
    const DWORD requested = static_cast<DWORD>(
        std::min<std::size_t>(buffer.size(), data.size() - offset));
    DWORD received = 0;
    if (!::ReadFile(handle, buffer.data(), requested, &received, nullptr) ||
        received != requested ||
        std::memcmp(buffer.data(), data.data() + offset, received) != 0) {
      errorText = QStringLiteral("wallet staging readback mismatch (Windows error %1)")
                      .arg(::GetLastError());
      return false;
    }
    offset += received;
  }
  return true;
}

bool validWindowsLeafName(const QString& name) {
  return !name.isEmpty() && name != QStringLiteral(".") &&
      name != QStringLiteral("..") && !name.endsWith(QLatin1Char('.')) &&
      !name.endsWith(QLatin1Char(' ')) &&
      !name.contains(QLatin1Char('/')) &&
      !name.contains(QLatin1Char('\\')) &&
      !name.contains(QLatin1Char(':')) && !name.contains(QChar::Null);
}

bool renameOpenFile(HANDLE handle, HANDLE destinationDirectory,
                    const QString& destinationName,
                    QString& errorText) {
  if (!validWindowsLeafName(destinationName)) {
    errorText = QStringLiteral("wallet destination filename is invalid");
    return false;
  }
  const std::wstring destination = destinationName.toStdWString();
  const DWORD nameBytes =
      static_cast<DWORD>(destination.size() * sizeof(wchar_t));
  const std::size_t infoSize = sizeof(FILE_RENAME_INFO) + nameBytes;
  std::vector<unsigned char> storage(infoSize, 0);
  FILE_RENAME_INFO* info =
      reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
  info->ReplaceIfExists = TRUE;
  info->RootDirectory = destinationDirectory;
  info->FileNameLength = nameBytes;
  std::memcpy(info->FileName, destination.data(), nameBytes);
  // SetFileInformationByHandle rejects a directory-relative RootDirectory on
  // supported Windows versions (ERROR_INVALID_PARAMETER). The underlying NT
  // rename operation supports it and keeps destination lookup anchored to the
  // already-opened, non-reparse parent directory.
  using NtSetInformationFileFn = LONG(NTAPI*)(
      HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);
  using RtlNtStatusToDosErrorFn = ULONG(WINAPI*)(LONG);
  const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
  const auto ntSetInformationFile = reinterpret_cast<NtSetInformationFileFn>(
      ntdll == nullptr
          ? nullptr
          : ::GetProcAddress(ntdll, "NtSetInformationFile"));
  const auto rtlNtStatusToDosError =
      reinterpret_cast<RtlNtStatusToDosErrorFn>(
          ntdll == nullptr
              ? nullptr
              : ::GetProcAddress(ntdll, "RtlNtStatusToDosError"));
  if (ntSetInformationFile == nullptr) {
    errorText = QStringLiteral("anchored Windows rename API is unavailable");
    return false;
  }
  IO_STATUS_BLOCK statusBlock{};
  constexpr ULONG FILE_RENAME_INFORMATION_CLASS = 10;
  const LONG status = ntSetInformationFile(
      handle, &statusBlock, info, static_cast<ULONG>(infoSize),
      FILE_RENAME_INFORMATION_CLASS);
  if (status < 0) {
    const DWORD error = rtlNtStatusToDosError == nullptr
        ? ERROR_GEN_FAILURE
        : static_cast<DWORD>(rtlNtStatusToDosError(status));
    errorText = QStringLiteral("atomic wallet rename failed (Windows error %1)")
                    .arg(error);
    return false;
  }
  return true;
}

bool writeWindowsAtomic(const QString& destinationPath,
                        const std::string& data,
                        QString& errorText) {
  PACL rawAcl = nullptr;
  if (!privateAcl(rawAcl, errorText)) {
    return false;
  }
  LocalPointer aclOwner(rawAcl);

  SECURITY_DESCRIPTOR descriptor{};
  if (!::InitializeSecurityDescriptor(&descriptor,
                                      SECURITY_DESCRIPTOR_REVISION) ||
      !::SetSecurityDescriptorDacl(&descriptor, TRUE, rawAcl, FALSE) ||
      !::SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED,
                                      SE_DACL_PROTECTED)) {
    errorText = QStringLiteral("private wallet security descriptor creation failed (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }
  SECURITY_ATTRIBUTES attributes{};
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = &descriptor;
  attributes.bInheritHandle = FALSE;

  const QFileInfo destinationInfo(destinationPath);
  const QDir directory = destinationInfo.absoluteDir();
  if (!validWindowsLeafName(destinationInfo.fileName())) {
    errorText = QStringLiteral("wallet destination filename is invalid");
    return false;
  }
  HANDLE directoryHandle = openDirectoryHandle(directory.absolutePath(), errorText);
  if (directoryHandle == INVALID_HANDLE_VALUE) {
    return false;
  }
  HANDLE handle = INVALID_HANDLE_VALUE;
  QString temporaryPath;
  std::wstring nativeTemporary;
  for (int attempt = 0; attempt < 32; ++attempt) {
    temporaryPath = directory.filePath(
        QStringLiteral(".discrete-wallet-save-%1.tmp")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    nativeTemporary = longWindowsPath(temporaryPath);
    handle = ::CreateFileW(
        nativeTemporary.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE |
                                    READ_CONTROL | WRITE_DAC,
        0, &attributes, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
            FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (handle != INVALID_HANDLE_VALUE || ::GetLastError() != ERROR_FILE_EXISTS) {
      break;
    }
  }
  if (handle == INVALID_HANDLE_VALUE) {
    ::CloseHandle(directoryHandle);
    errorText = QStringLiteral("cannot create private wallet staging file (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }

  bool renamed = false;
  auto fail = [&](const QString& message) {
    errorText = message;
    ::CloseHandle(handle);
    ::CloseHandle(directoryHandle);
    if (!renamed) {
      ::DeleteFileW(nativeTemporary.c_str());
    }
    handle = INVALID_HANDLE_VALUE;
    return false;
  };

  if (!verifyHandle(handle, errorText)) {
    return fail(errorText);
  }
  std::size_t offset = 0;
  while (offset < data.size()) {
    const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
        data.size() - offset, static_cast<std::size_t>(MAXDWORD)));
    DWORD written = 0;
    if (!::WriteFile(handle, data.data() + offset, requested, &written,
                     nullptr) || written != requested) {
      return fail(QStringLiteral("wallet staging write failed (Windows error %1)")
                      .arg(::GetLastError()));
    }
    offset += written;
  }
  if (!::FlushFileBuffers(handle)) {
    return fail(QStringLiteral("wallet staging flush failed (Windows error %1)")
                    .arg(::GetLastError()));
  }
  if (!readbackMatches(handle, data, errorText)) {
    return fail(errorText);
  }
  if (!renameOpenFile(handle, directoryHandle, destinationInfo.fileName(),
                      errorText)) {
    return fail(errorText);
  }
  renamed = true;
  // The same open handle was already flushed, read back, and validated. The
  // rooted rename changes only its directory entry, so there is no fallible
  // post-commit step whose failure could be misreported as "not committed".
  ::CloseHandle(handle);
  ::CloseHandle(directoryHandle);
  handle = INVALID_HANDLE_VALUE;
  return true;
}

bool replaceWindowsPrivateFile(const QString& replacementPath,
                               const QString& destinationPath,
                               QString& errorText) {
  const QFileInfo replacementInfo(replacementPath);
  const QFileInfo destinationInfo(destinationPath);
  if (!validWindowsLeafName(replacementInfo.fileName()) ||
      !validWindowsLeafName(destinationInfo.fileName())) {
    errorText = QStringLiteral("private wallet replacement filename is invalid");
    return false;
  }
  const QString replacementDirectory =
      QDir::cleanPath(replacementInfo.absolutePath());
  const QString destinationDirectory =
      QDir::cleanPath(destinationInfo.absolutePath());
  if (replacementDirectory.compare(destinationDirectory,
                                   Qt::CaseInsensitive) != 0) {
    errorText = QStringLiteral("private wallet replacement must stay in one directory");
    return false;
  }

  HANDLE directoryHandle =
      openDirectoryHandle(destinationDirectory, errorText);
  if (directoryHandle == INVALID_HANDLE_VALUE) {
    return false;
  }
  const std::wstring nativeReplacement = longWindowsPath(replacementPath);
  HANDLE replacement = ::CreateFileW(
      nativeReplacement.c_str(),
      GENERIC_READ | GENERIC_WRITE | DELETE | READ_CONTROL, 0,
      nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (replacement == INVALID_HANDLE_VALUE) {
    const DWORD error = ::GetLastError();
    ::CloseHandle(directoryHandle);
    errorText = QStringLiteral("cannot open private wallet replacement (Windows error %1)")
                    .arg(error);
    return false;
  }

  bool valid = verifyHandle(replacement, errorText);
  if (valid) {
    valid = renameOpenFile(replacement, directoryHandle,
                           destinationInfo.fileName(), errorText);
  }
  ::CloseHandle(replacement);
  ::CloseHandle(directoryHandle);
  return valid;
}

#else

bool verifyDescriptor(int descriptor, QString& errorText) {
  struct stat info {};
  if (::fstat(descriptor, &info) != 0) {
    errorText = QStringLiteral("fstat failed: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  if (!S_ISREG(info.st_mode) || info.st_nlink != 1 ||
      info.st_uid != ::geteuid() || (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
    errorText = QStringLiteral("wallet destination is not a private single-link regular file");
    return false;
  }
  return true;
}

struct PosixPath {
  QByteArray directory;
  QByteArray name;
};

bool splitPosixPath(const QString& path, PosixPath& result,
                    QString& errorText) {
  const QFileInfo info(path);
  const QString name = info.fileName();
  if (name.isEmpty() || name == QStringLiteral(".") ||
      name == QStringLiteral("..") || name.contains(QLatin1Char('/'))) {
    errorText = QStringLiteral("wallet destination filename is invalid");
    return false;
  }
  result.directory = QFile::encodeName(info.absolutePath());
  result.name = QFile::encodeName(name);
  return true;
}

int openSafeDirectory(const QByteArray& path, QString& errorText) {
  const int descriptor = ::open(path.constData(),
                                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0) {
    errorText = QStringLiteral("cannot open the wallet directory securely: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return -1;
  }
  struct stat info {};
  if (::fstat(descriptor, &info) != 0 || !S_ISDIR(info.st_mode) ||
      info.st_uid != ::geteuid() ||
      (info.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
    ::close(descriptor);
    errorText = QStringLiteral("wallet parent must be an owner-controlled non-writable directory");
    return -1;
  }
  return descriptor;
}

bool writeAll(int descriptor, const std::string& data, QString& errorText) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written = ::write(descriptor, data.data() + offset,
                                    data.size() - offset);
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      errorText = QStringLiteral("wallet staging write failed: %1")
                      .arg(QString::fromLocal8Bit(std::strerror(errno)));
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

bool readbackMatches(int descriptor, const std::string& data,
                     QString& errorText) {
  if (::lseek(descriptor, 0, SEEK_SET) < 0) {
    errorText = QStringLiteral("wallet staging seek failed: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  std::vector<char> buffer(64 * 1024);
  std::size_t offset = 0;
  while (offset < data.size()) {
    const std::size_t request =
        std::min(buffer.size(), data.size() - offset);
    const ssize_t received = ::read(descriptor, buffer.data(), request);
    if (received < 0 && errno == EINTR) {
      continue;
    }
    if (received <= 0 ||
        std::memcmp(buffer.data(), data.data() + offset,
                    static_cast<std::size_t>(received)) != 0) {
      errorText = QStringLiteral("wallet staging readback mismatch");
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  return true;
}

bool destinationCanBeReplaced(int directory, const QByteArray& name,
                              QString& errorText) {
  struct stat info {};
  if (::fstatat(directory, name.constData(), &info,
                AT_SYMLINK_NOFOLLOW) == 0) {
    if (S_ISDIR(info.st_mode)) {
      errorText = QStringLiteral("wallet destination is a directory");
      return false;
    }
    return true;
  }
  if (errno == ENOENT) {
    return true;
  }
  errorText = QStringLiteral("cannot inspect wallet destination: %1")
                  .arg(QString::fromLocal8Bit(std::strerror(errno)));
  return false;
}

bool sameOpenedFile(int descriptor, int directory, const QByteArray& name,
                    QString& errorText) {
  struct stat opened {};
  struct stat named {};
  if (::fstat(descriptor, &opened) != 0 ||
      ::fstatat(directory, name.constData(), &named,
                AT_SYMLINK_NOFOLLOW) != 0 ||
      opened.st_dev != named.st_dev || opened.st_ino != named.st_ino) {
    errorText = QStringLiteral("wallet staging pathname changed during commit");
    return false;
  }
  return true;
}

bool syncDirectory(int directory, QString& errorText) {
  if (::fsync(directory) == 0) {
    return true;
  }
  errorText = QStringLiteral("wallet directory sync failed: %1")
                  .arg(QString::fromLocal8Bit(std::strerror(errno)));
  return false;
}

bool writePosixAtomic(const QString& destinationPath,
                      const std::string& data, QString& errorText) {
  PosixPath destination;
  if (!splitPosixPath(destinationPath, destination, errorText)) {
    return false;
  }
  const int directory = openSafeDirectory(destination.directory, errorText);
  if (directory < 0) {
    return false;
  }

  QByteArray temporaryName;
  int file = -1;
  for (int attempt = 0; attempt < 32; ++attempt) {
    temporaryName = QByteArrayLiteral(".discrete-wallet-save-") +
        QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1() +
        QByteArrayLiteral(".tmp");
    file = ::openat(directory, temporaryName.constData(),
                    O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                    S_IRUSR | S_IWUSR);
    if (file >= 0 || errno != EEXIST) {
      break;
    }
  }
  if (file < 0) {
    const int savedError = errno;
    ::close(directory);
    errorText = QStringLiteral("cannot create private wallet staging file: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(savedError)));
    return false;
  }

  bool renamed = false;
  auto finish = [&](bool result) {
    if (!renamed) {
      ::unlinkat(directory, temporaryName.constData(), 0);
    }
    ::close(file);
    ::close(directory);
    return result;
  };

  if (!verifyDescriptor(file, errorText) ||
      !writeAll(file, data, errorText) || ::fsync(file) != 0 ||
      !readbackMatches(file, data, errorText) ||
      !sameOpenedFile(file, directory, temporaryName, errorText) ||
      !destinationCanBeReplaced(directory, destination.name, errorText)) {
    if (errorText.isEmpty()) {
      errorText = QStringLiteral("wallet staging flush failed: %1")
                      .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }
    return finish(false);
  }
  if (::renameat(directory, temporaryName.constData(), directory,
                 destination.name.constData()) != 0) {
    errorText = QStringLiteral("atomic wallet rename failed: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return finish(false);
  }
  renamed = true;
  if (!sameOpenedFile(file, directory, destination.name, errorText) ||
      !syncDirectory(directory, errorText)) {
    return finish(false);
  }
  return finish(true);
}

bool replacePosixPrivateFile(const QString& replacementPath,
                             const QString& destinationPath,
                             QString& errorText) {
  PosixPath replacement;
  PosixPath destination;
  if (!splitPosixPath(replacementPath, replacement, errorText) ||
      !splitPosixPath(destinationPath, destination, errorText) ||
      replacement.directory != destination.directory) {
    if (errorText.isEmpty()) {
      errorText = QStringLiteral("private wallet replacement must stay in one directory");
    }
    return false;
  }
  const int directory = openSafeDirectory(destination.directory, errorText);
  if (directory < 0) {
    return false;
  }
  const int replacementFile =
      ::openat(directory, replacement.name.constData(),
               O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (replacementFile < 0) {
    const int savedError = errno;
    ::close(directory);
    errorText = QStringLiteral("cannot open private wallet replacement: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(savedError)));
    return false;
  }

  bool valid = verifyDescriptor(replacementFile, errorText) &&
      sameOpenedFile(replacementFile, directory, replacement.name, errorText) &&
      destinationCanBeReplaced(directory, destination.name, errorText);
  if (valid && ::renameat(directory, replacement.name.constData(), directory,
                          destination.name.constData()) != 0) {
    errorText = QStringLiteral("atomic wallet rename failed: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    valid = false;
  }
  if (valid) {
    valid = sameOpenedFile(replacementFile, directory, destination.name,
                           errorText) &&
        syncDirectory(directory, errorText);
  }
  ::close(replacementFile);
  ::close(directory);
  return valid;
}

#endif

}  // namespace

bool AtomicWalletFile::write(const QString& destinationPath,
                             const std::string& data,
                             QString& errorText) {
  errorText.clear();
  if (destinationPath.isEmpty()) {
    errorText = QStringLiteral("wallet destination path is empty");
    return false;
  }
  if (data.size() > static_cast<std::size_t>(
                        std::numeric_limits<qint64>::max())) {
    errorText = QStringLiteral("serialized wallet is too large to write");
    return false;
  }

#ifdef Q_OS_WIN
  if (!writeWindowsAtomic(destinationPath, data, errorText)) {
    return false;
  }
#else
  if (!writePosixAtomic(destinationPath, data, errorText)) {
    return false;
  }
#endif
  return true;
}

bool AtomicWalletFile::replacePrivateFile(const QString& replacementPath,
                                          const QString& destinationPath,
                                          QString& errorText) {
  errorText.clear();
  if (replacementPath.isEmpty() || destinationPath.isEmpty() ||
      QFileInfo(replacementPath).absoluteFilePath() ==
          QFileInfo(destinationPath).absoluteFilePath()) {
    errorText = QStringLiteral("wallet replacement paths are invalid");
    return false;
  }
#ifdef Q_OS_WIN
  return replaceWindowsPrivateFile(replacementPath, destinationPath, errorText);
#else
  return replacePosixPrivateFile(replacementPath, destinationPath, errorText);
#endif
}

bool AtomicWalletFile::isPrivateRegularFile(const QString& path,
                                            QString& errorText) {
  errorText.clear();
#ifdef Q_OS_WIN
  const std::wstring nativePath = longWindowsPath(path);
  HANDLE handle = ::CreateFileW(
      nativePath.c_str(),
      FILE_READ_ATTRIBUTES | READ_CONTROL,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    errorText = QStringLiteral("cannot inspect wallet file (Windows error %1)")
                    .arg(::GetLastError());
    return false;
  }
  const bool valid = verifyHandle(handle, errorText);
  ::CloseHandle(handle);
  return valid;
#else
  const QByteArray encoded = QFile::encodeName(path);
  const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC |
                                                       O_NOFOLLOW);
  if (descriptor < 0) {
    errorText = QStringLiteral("cannot inspect wallet file: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno)));
    return false;
  }
  const bool valid = verifyDescriptor(descriptor, errorText);
  ::close(descriptor);
  return valid;
#endif
}

}  // namespace WalletGui
