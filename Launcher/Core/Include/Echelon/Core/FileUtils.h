// Echelon @feature Codex 16/10/2026 core FileUtils
/*
** Filesystem helpers shared by every launcher layer.
**
** IsInsideRoot carries over the security canon from
** Launcher/LauncherInstaller.cpp (twinned in LauncherMods.cpp): a
** component-wise prefix comparison that refuses weak string-prefix matches
** ("/root-evil" never matches root "/root"). Both paths are additionally
** run through lexically_normal() so lexical ".." escapes such as
** "root/../escape" collapse and are rejected; the monolithic callers fed
** already-canonical paths, while the core layer accepts caller-shaped input.
**
** Sha256HexFile mirrors DigestFile from Launcher/LauncherInstaller.cpp:
** streaming EVP with a 1 MiB buffer and lowercase hex output. A missing or
** unreadable file is an ErrorStage::IO error, never an empty string, so
** "no digest" cannot be mistaken for a real hash.
*/

#pragma once

#include "Echelon/Core/Result.h"

#include <filesystem>
#include <string>

namespace echelon::core
{

// Strong containment check: candidate sits at or below root (equality
// counts as inside). Weak string-prefix matches and lexical ".." escapes
// are rejected. Purely lexical; no filesystem access is performed.
bool IsInsideRoot(const std::filesystem::path &root, const std::filesystem::path &candidate);

// Reads the entire file into memory. A missing or unreadable file yields
// an ErrorStage::IO / ErrorCode::FilesystemFailure error.
Result<std::string> ReadFile(const std::filesystem::path &path);

// Plain truncate-and-write for non-critical payloads and tests. The parent
// directory must already exist; durability-critical callers use
// AtomicWriteFile instead.
Status WriteFile(const std::filesystem::path &path, const std::string &bytes);

// Durable atomic write. The payload is written to "<name>.tmp-<pid>" in
// the SAME directory, fsynced, rename()d over the target, and the
// directory is fsynced so the rename itself survives a crash. On failure
// the temp file is removed and ErrorStage::IO /
// ErrorCode::FilesystemFailure is returned. Leftover "<name>.tmp-*" files
// from earlier crashed writes are reaped before writing, except temps
// whose embedded pid still belongs to a live process. Callers writing the
// same target concurrently must serialize themselves (see FsLock).
Status AtomicWriteFile(const std::filesystem::path &path, const std::string &bytes);

// Lowercase hex SHA-256 of a file, streaming via EVP with a 1 MiB buffer
// (mirrors DigestFile in Launcher/LauncherInstaller.cpp). A missing or
// unreadable file yields ErrorStage::IO; a digest-computation failure
// yields ErrorStage::Hash.
Result<std::string> Sha256HexFile(const std::filesystem::path &path);

// Creates the directory and any missing parents. An already-existing
// directory (or file) counts as success; returns false only when the
// directory does not exist afterwards.
bool EnsureDir(const std::filesystem::path &path);

} // namespace echelon::core
