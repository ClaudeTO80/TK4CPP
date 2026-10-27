#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>
#include "TK4CPP/Utility/FileHandler.h"

#include <system_error>

using namespace TK4CPP;
using namespace TK4CPP::Utility;

bool FileHandler::exists(const std::string& path)
{
	return exists(std::filesystem::path(path));
}
bool FileHandler::exists(const std::filesystem::path& path)
{
	return std::filesystem::exists(path);
}

bool FileHandler::isFile(const std::string& path) {
	std::error_code ec;
	return std::filesystem::is_regular_file(std::filesystem::path(path), ec);
}

bool FileHandler::isFile(const std::filesystem::path& path) {
	return std::filesystem::is_regular_file(path);
}

std::filesystem::path FileHandler::concatNames(const std::filesystem::path& parent, const std::filesystem::path& child)
{
	return parent / child;
}

std::string FileHandler::getFullName(const std::string& path) {
	try {
		return std::filesystem::absolute(path).string();
	}
	catch (const std::filesystem::filesystem_error&) {
		return path;
	}
}

std::string FileHandler::getFileName(const std::filesystem::path& path)
{
	std::string empty("");
	return path.has_filename() ? path.filename().string() : empty;
}

std::string FileHandler::getFileName(const std::string& path)
{
	return getFileName(std::filesystem::path(path));
}

std::string FileHandler::getExtension(const std::filesystem::path& path)
{
	std::string empty("");
	return path.has_extension() ? path.extension().string() : empty;
}

std::string FileHandler::getExtension(const std::string& path)
{
	return getExtension(std::filesystem::path(path));
}

std::string FileHandler::sizeToString(unsigned long long size)
{
	struct Unit { unsigned long long divisor; const char* label; };
	static constexpr Unit units[] = {
		{ m_EB, m_EBStr }, { m_PB, m_PBStr }, { m_TB, m_TBStr },
		{ m_GB, m_GBStr }, { m_MB, m_MBStr }, { m_KB, m_KBStr },
	};

	std::stringstream ss;
	for (const auto& unit : units) {
		if (size >= unit.divisor) {
			ss << 1. * size / unit.divisor << " " << unit.label;
			return ss.str();
		}
	}
	ss << size << " " << m_BStr;
	return ss.str();
}

std::filesystem::path FileHandler::writeStringToFile(const std::string& path, const std::string& content, WriteBehavior behavior) {
	return writeStringToFile(std::filesystem::path(path), content, behavior);
}

std::filesystem::path FileHandler::writeStringToFile(const std::filesystem::path& path, const std::string& content, WriteBehavior behavior) {
	namespace fs = std::filesystem;
	fs::path filePath(path);

	auto fileExists = fs::exists(filePath);

	// Helper to generate a new file name with _ddd suffix
	auto generateSuffixedName = [](const fs::path& basePath) -> fs::path {
		fs::path dir = basePath.parent_path();
		std::string stem = basePath.stem().string();
		std::string ext = basePath.extension().string();
		int counter = 1;
		fs::path candidate;
		do {
			char suffix[16];
			std::snprintf(suffix, sizeof(suffix), "_%03d", counter++);
			candidate = dir / (stem + suffix + ext);
		} while (fs::exists(candidate));
		return candidate;
		};

	if (fileExists) {
		switch (behavior) {
		case WriteBehavior::Overwrite:
			break; // Proceed to overwrite
		case WriteBehavior::RenameExisting: {
			// Usa FileHandler::exists per verificare l'esistenza
			if (FileHandler::exists(filePath.string())) {
				fs::path newName = generateSuffixedName(filePath);
				// Usa FileHandler::moveFile per rinominare
				if (!FileHandler::moveFile(filePath.string(), newName.string()).first) {
					return ""; // Fallito il rename
				}
			}
			break;
		}
		case WriteBehavior::RenameNew: {
			fs::path newName = generateSuffixedName(filePath);
			std::ofstream ofs(newName.string(), std::ios::out | std::ios::trunc | std::ios::binary);
			if (!ofs) return "";
			if (!robustWrite(ofs, content)) return "";
			return newName.string();
		}
		case WriteBehavior::Skip:
			return ""; // Do not write
		}
	}

	// Write to the original path
	std::ofstream ofs(path, std::ios::out | std::ios::trunc | std::ios::binary);
	if (!ofs) return "";
	if (!robustWrite(ofs, content)) return "";
	return path;
}

bool FileHandler::robustWrite(std::ofstream& ofs, const std::string& content) {
	try {
		ofs.exceptions(std::ofstream::failbit | std::ofstream::badbit);
		ofs << content;
		return true;
	}
	catch (const std::ios_base::failure& ex) {
		std::cerr << "File write error: " << ex.what() << std::endl;
	}
	catch (const std::bad_alloc& ex) {
		std::cerr << "Memory allocation error during file write: " << ex.what() << std::endl;
	}
	catch (const std::exception& ex) {
		std::cerr << "Unexpected error during file write: " << ex.what() << std::endl;
	}
	catch (...) {
		std::cerr << "Unknown error during file write." << std::endl;
	}
	return false;
}
std::string FileHandler::readStringFromFile(const std::string& path) {
	std::ifstream ifs(path, std::ios::in | std::ios::binary);
	if (!ifs) return "";
	std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
	return content;
}

std::shared_ptr<std::vector<std::byte>> FileHandler::readByteFromFile(const std::string& path)
{
	if (path.empty()) return nullptr;

	std::ifstream ifs(path, std::ios::binary | std::ios::ate);
	if (!ifs) return nullptr;

	const auto endPos = ifs.tellg();
	if (endPos < 0) return nullptr;

	const auto sz = static_cast<std::size_t>(endPos);
	ifs.seekg(0, std::ios::beg);
	if (!ifs) return nullptr;

	auto buf = std::make_shared<std::vector<std::byte>>(sz);
	if (sz == 0) return buf;

	if (!ifs.read(reinterpret_cast<char*>(buf->data()),
		static_cast<std::streamsize>(sz)))
		return nullptr; // Return nullptr on read failure

	return buf;
}

std::vector<std::string> FileHandler::listFiles(const std::string& folderPath)
{
	std::vector<std::string> files;
	try {
		std::filesystem::path dirPath(folderPath);
		if (!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
			return files;

		for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
			if (entry.is_regular_file()) {
				files.push_back(entry.path().filename().string());
			}
		}
	}
	catch (const std::filesystem::filesystem_error&) {
		// Ignore errors, return empty vector
	}
	return files;
}

std::pair<bool, std::string> FileHandler::removeFile(const std::string& filePath)
{
	// Validate input path
	if (filePath.empty()) {
		return { false, "File path is empty." };
	}
	return removeFile(std::filesystem::path(filePath));
}

std::pair<bool, std::string> FileHandler::removeFile(const std::filesystem::path& filePath)
{
	std::pair<bool, std::string> output(false, "");
	try {
		// Check for empty path
		if (filePath.empty()) {
			output.second = "File path is empty.";
			return output;
		}

		// Check existence and type
		if (!std::filesystem::exists(filePath)) {
			output.second = "File does not exist.";
			return output;
		}
		if (!std::filesystem::is_regular_file(filePath)) {
			output.second = "Path exists but is not a regular file.";
			return output;
		}

		// Attempt removal
		output.first = std::filesystem::remove(filePath);
		if (!output.first) {
			output.second = "File could not be removed (unknown reason).";
		}
	}
	catch (const std::filesystem::filesystem_error& e) {
		output.second = std::string("Filesystem error: ") + e.what();
	}
	catch (const std::exception& e) {
		output.second = std::string("Exception: ") + e.what();
	}
	catch (...) {
		output.second = "Unknown error during file removal.";
	}

	return output;
}

std::pair<bool, std::string> FileHandler::copyFile(const std::string& sourcePath, const std::string& destPath, CopyBehavior behavior)
{
	std::pair<bool, std::string> output(false, "");
	namespace fs = std::filesystem;
	fs::path dst(destPath);

	// Helper to generate a new file name with _ddd suffix
	auto generateSuffixedName = [](const fs::path& basePath) -> fs::path {
		fs::path dir = basePath.parent_path();
		std::string stem = basePath.stem().string();
		std::string ext = basePath.extension().string();
		int counter = 1;
		fs::path candidate;
		do {
			char suffix[16];
			std::snprintf(suffix, sizeof(suffix), "_%03d", counter++);
			candidate = dir / (stem + suffix + ext);
		} while (fs::exists(candidate));
		return candidate;
		};

	try {
		if (fs::exists(dst)) {
			switch (behavior) {
			case CopyBehavior::FailIfExists:
				output.first = false;
				output.second = "Destination file exists.";
				return output;
			case CopyBehavior::OverwriteIfExists:
				output.first = fs::copy_file(sourcePath, destPath, fs::copy_options::overwrite_existing);
				return output;
			case CopyBehavior::RenameExisting: {
				fs::path newName = generateSuffixedName(dst);
				if (std::rename(destPath.c_str(), newName.string().c_str()) != 0) {
					output.first = false;
					output.second = "Failed to rename existing file.";
					return output;
				}
				output.first = fs::copy_file(sourcePath, destPath, fs::copy_options::none);
				return output;
			}
			case CopyBehavior::RenameNew: {
				fs::path newName = generateSuffixedName(dst);
				output.first = fs::copy_file(sourcePath, newName.string(), fs::copy_options::none);
				return output;
			}
			}
		}
		else {
			output.first = fs::copy_file(sourcePath, destPath, fs::copy_options::none);
			return output;
		}
	}
	catch (const fs::filesystem_error& e) {
		output.second = e.what();
	}
	return output;
}

std::pair<bool, std::string> FileHandler::moveFile(const std::string& sourcePath, const std::string& destPath, WriteBehavior behavior)
{
	using namespace std::filesystem;

	// Helper to generate a new file name with _ddd suffix
	auto generateSuffixedName = [](const path& basePath) -> path {
		path dir = basePath.parent_path();
		std::string stem = basePath.stem().string();
		std::string ext = basePath.extension().string();
		int counter = 1;
		path candidate;
		do {
			char suffix[16];
			std::snprintf(suffix, sizeof(suffix), "_%03d", counter++);
			candidate = dir / (stem + suffix + ext);
		} while (exists(candidate));
		return candidate;
		};

	try {
		if (!FileHandler::exists(sourcePath) || !FileHandler::isFile(sourcePath)) {
			return { false, "Source file does not exist or is not a regular file." };
		}
		if (FileHandler::exists(destPath)) {
			if (!FileHandler::isFile(destPath)) {
				return { false, "Destination path exists and is not a regular file." };
			}
			switch (behavior) {
			case WriteBehavior::Overwrite:
				std::filesystem::remove(destPath);
				break;
			case WriteBehavior::RenameExisting: {
				std::filesystem::path newName = generateSuffixedName(destPath);
				if (std::rename(destPath.c_str(), newName.string().c_str()) != 0) {
					return { false, "Failed to rename existing destination file." };
				}
				break;
			}
			case WriteBehavior::RenameNew: {
				std::filesystem::path newName = generateSuffixedName(destPath);
				if (std::rename(sourcePath.c_str(), newName.string().c_str()) != 0) {
					return { false, "Failed to rename new file to avoid overwrite." };
				}
				return { true, "" };
			}
			case WriteBehavior::Skip:
				return { false, "Destination file exists. Operation skipped." };
			}
		}
		std::filesystem::rename(sourcePath, destPath);
		return { true, "" };

	}
	catch (const std::filesystem::filesystem_error& e) {
		return { false, e.what() };
	}
}

unsigned long long FileHandler::getFileSize(const std::string& filePath)
{
	return getFileSize(std::filesystem::path(filePath));
}

unsigned long long FileHandler::getFileSize(const std::filesystem::path& filePath)
{
	return getFileSize(filePath, true);
}

unsigned long long FileHandler::getFileSize(const std::filesystem::path& filePath, bool check)
{
	if (!FileHandler::exists(filePath) || (check && !FileHandler::isFile(filePath)))
		return -1L;
	try {
		return static_cast<unsigned long long>(std::filesystem::file_size(filePath));
	}
	catch (const std::filesystem::filesystem_error&) {
            return -1L;
	}
}

std::filesystem::path FileHandler::getCurrentPath()
{
	return std::filesystem::current_path();
}

std::filesystem::path FileHandler::findChild(const std::filesystem::path& parent, const std::string& childName, bool recursive)
{
	std::filesystem::path empty{ "" };
	try {
		if (recursive) {
			for (const auto& entry : std::filesystem::recursive_directory_iterator(parent)) {
				if (entry.path().filename() == childName) {
					return entry.path();
				}
			}
		}
		else {
			for (const auto& entry : std::filesystem::directory_iterator(parent)) {
				if (entry.path().filename() == childName) {
					return entry.path();
				}
			}
		}
	}
	catch (const std::filesystem::filesystem_error&) {
		// Ignore errors
	}

	return empty;
}
