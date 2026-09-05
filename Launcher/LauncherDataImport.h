#pragma once

#include <filesystem>
#include <string>

namespace EchelonLauncher
{

// Copy retail assets without changing the source or overwriting existing imported data.
bool CopyRetailDataTree(const std::filesystem::path &source, const std::filesystem::path &destination,
	const std::filesystem::path &backupRoot, const std::filesystem::path &journal, std::string &errorMessage);

}
