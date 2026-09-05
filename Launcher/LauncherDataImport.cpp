// Echelon @bugfix Codex 05/09/2026 Preserve selected retail installations during import.
#include "LauncherDataImport.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace EchelonLauncher
{
namespace
{

bool IsInside(const fs::path &candidate, const fs::path &root)
{
	auto part = candidate.begin();
	for (const auto &rootPart : root) {
		if (part == candidate.end() || *part++ != rootPart) return false;
	}
	return true;
}

bool FilesEqual(const fs::path &left, const fs::path &right)
{
	std::ifstream leftFile(left, std::ios::binary), rightFile(right, std::ios::binary);
	if (!leftFile || !rightFile) return false;
	std::array<char, 65536> leftBuffer{}, rightBuffer{};
	do {
		leftFile.read(leftBuffer.data(), leftBuffer.size());
		rightFile.read(rightBuffer.data(), rightBuffer.size());
		if (leftFile.bad() || rightFile.bad() || leftFile.gcount() != rightFile.gcount() ||
			!std::equal(leftBuffer.begin(), leftBuffer.begin() + leftFile.gcount(), rightBuffer.begin())) return false;
	} while (leftFile && rightFile);
	return leftFile.eof() && rightFile.eof();
}

bool CreateDirectoryInside(const fs::path &path, const fs::path &root, std::error_code &error)
{
	const fs::path resolved = fs::weakly_canonical(path, error);
	if (error) return false;
	if (!IsInside(resolved, root)) {
		error = std::make_error_code(std::errc::permission_denied);
		return false;
	}
	fs::create_directories(path, error);
	return !error;
}

bool CopyFileVerified(const fs::path &source, const fs::path &target, const fs::path &root,
	std::string &errorMessage)
{
	std::error_code error;
	if (!CreateDirectoryInside(target.parent_path(), root, error)) {
		errorMessage = "Cannot create import directory: " + error.message();
		return false;
	}
	const fs::file_status status = fs::symlink_status(target, error);
	if (error == std::errc::no_such_file_or_directory) error.clear();
	if (!error && fs::is_regular_file(status) && FilesEqual(source, target)) return true;
	if (error) { errorMessage = error.message(); return false; }
	// Copy file symlink contents into independent regular files; never rename/delete the source.
	if (!fs::copy_file(source, target, fs::copy_options::none, error) || error) {
		errorMessage = "Cannot copy " + source.string() + ": " + error.message();
		return false;
	}
	if (!FilesEqual(source, target)) {
		errorMessage = "Imported file verification failed: " + target.string();
		return false;
	}
	return true;
}

}

bool CopyRetailDataTree(const fs::path &source, const fs::path &destination,
	const fs::path &backupRoot, const fs::path &journal, std::string &errorMessage)
{
	errorMessage.clear();
	std::error_code error;
	const fs::path sourceRoot = fs::canonical(source, error);
	if (error || !fs::is_directory(sourceRoot, error) || error) {
		errorMessage = "Cannot read retail source directory";
		return false;
	}
	const fs::path destinationRoot = fs::weakly_canonical(destination, error);
	if (error) { errorMessage = error.message(); return false; }
	const fs::path conflictRoot = fs::weakly_canonical(backupRoot, error);
	if (error) { errorMessage = error.message(); return false; }
	if (IsInside(destinationRoot, sourceRoot) || IsInside(sourceRoot, destinationRoot) ||
		IsInside(conflictRoot, sourceRoot) || IsInside(sourceRoot, conflictRoot)) {
		errorMessage = "Retail source and import destinations must not overlap";
		return false;
	}
	if (!CreateDirectoryInside(destinationRoot, destinationRoot, error)) {
		errorMessage = error.message();
		return false;
	}
	std::ofstream journalOutput(journal, std::ios::app);
	fs::recursive_directory_iterator iterator(sourceRoot, error), end;
	for (; !error && iterator != end; iterator.increment(error)) {
		const fs::directory_entry &entry = *iterator;
		const fs::path relative = entry.path().lexically_relative(sourceRoot);
		const fs::path target = destinationRoot / relative;
		if (entry.is_directory(error)) {
			if (entry.is_symlink(error)) {
				errorMessage = "Select real directories instead of nested directory symlinks: " + entry.path().string();
				return false;
			}
			if (error || !CreateDirectoryInside(target, destinationRoot, error)) break;
			continue;
		}
		if (error || !entry.is_regular_file(error)) {
			errorMessage = "Cannot import non-regular retail file: " + entry.path().string();
			return false;
		}
		if (!CreateDirectoryInside(target.parent_path(), destinationRoot, error)) break;
		const fs::file_status targetStatus = fs::symlink_status(target, error);
		if (error == std::errc::no_such_file_or_directory) error.clear();
		if (error) break;
		if (fs::is_regular_file(targetStatus) && FilesEqual(entry.path(), target)) continue;
		const bool conflict = fs::exists(targetStatus);
		const fs::path copyTarget = conflict ? conflictRoot / relative : target;
		if (!CopyFileVerified(entry.path(), copyTarget, conflict ? conflictRoot : destinationRoot, errorMessage)) return false;
		if (journalOutput) journalOutput << "copied " << entry.path().string() << " -> " << copyTarget.string() << '\n';
	}
	if (error) {
		errorMessage = "Cannot import retail data: " + error.message();
		return false;
	}
	return true;
}

}
