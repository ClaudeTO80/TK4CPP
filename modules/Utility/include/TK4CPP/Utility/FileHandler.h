#pragma once
#include <string>
#include "TK4CPP/Utility/Export.h"
#include <vector>
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

namespace TK4CPP
{
	namespace Utility
	{
		// FILEHANDLER.H

		 /**
		  * @brief Provides static utility methods for file management.
		  * @details
		  * The FileHandler class offers a comprehensive set of static methods for file operations,
		  * including existence checks, type queries, reading and writing file contents, listing files,
		  * removing, copying and moving files, and obtaining file sizes.
		  * Folder management (creation, removal, listing of subfolders, type checks) lives in FolderHandler.
		  * It supports both std::string and std::filesystem::path for path arguments where appropriate.
		  *
		  * Key features:
		  * - Existence and type checks for files.
		  * - Reading and writing file contents as strings.
		  * - Listing the files of a directory.
		  * - Removing files.
		  * - Copying files with configurable behavior if the destination exists.
		  * - Moving (renaming) files.
		  * - Getting file sizes.
		  *
		  * All methods are static and designed for convenience and safety, catching exceptions where applicable.
		  * Platform-specific APIs (stat, DIR, dirent) are used for compatibility, and std::filesystem is leveraged for modern features.
		  *
		  * @since 0.1.0.0
		  */
		class TK4CPP_UTILITY_EXPORT FileHandler
		{
		public:


			struct FileInfo {
				std::string name;
				uintmax_t size;
			};

			struct FolderInfo {
				std::string path;
				uintmax_t totalSize = 0;
				std::vector<FileInfo> files;
			};


		public:

			static const unsigned long long m_B  = 1ULL;
			static const unsigned long long m_KB = 1024ULL;
			static const unsigned long long m_MB = 1024ULL * 1024ULL;
			static const unsigned long long m_GB = 1024ULL * 1024ULL * 1024ULL;
			static const unsigned long long m_TB = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
			static const unsigned long long m_PB = 1024ULL * 1024ULL * 1024ULL * 1024ULL* 1024ULL;
			static const unsigned long long m_EB = 1024ULL * 1024ULL * 1024ULL * 1024ULL* 1024ULL* 1024ULL;

			static constexpr const char* m_BStr ="Byte";
			static constexpr const char* m_KBStr="KB";
			static constexpr const char* m_MBStr="MB";
			static constexpr const char* m_GBStr="GB";
			static constexpr const char* m_TBStr="TB";
			static constexpr const char* m_PBStr="PB";
			static constexpr const char* m_EBStr="EB";

		public:

			/**
			 * @brief Checks if a file or directory exists at the given path.
			 * @param path Path to check.
			 * @return true if the file or directory exists, false otherwise.
			 * @since 0.1.0.0

			 */
			static bool exists(const std::string& path);

			/**
			 * @brief Checks if a file or directory exists at the given filesystem path.
			 * @param path std::filesystem::path object to check.
			 * @return true if the file or directory exists, false otherwise.
			 * @since 0.1.0.0
			 */
			static bool exists(const std::filesystem::path& path);

			/**
			 * @brief Determines if the given path is a regular file.
			 * @param path Path to check.
			 * @return true if the path is a file, false otherwise.
			 * @since 0.1.0.0
			 */
			static bool isFile(const std::string& path);

			static bool isFile(const std::filesystem::path& path);
			
			static std::filesystem::path concatNames(const std::filesystem::path& parent, const std::filesystem::path& child);

			/**
			 * @brief Gets the absolute path of the given file or directory.
			 * @param path Relative or absolute path.
			 * @return Absolute path as a string. If an error occurs, returns the input path.
			 * @since 0.1.0.0
			 */
			static std::string getFullName(const std::string& path);

			static std::string getFileName(const std::filesystem::path& path);

			static std::string getFileName(const std::string& path);

			static std::string getExtension(const std::filesystem::path& path);

			static std::string getExtension(const std::string& path);

			static std::string sizeToString(unsigned long long size);

			/**
			 * @brief Specifies behavior when the destination file already exists.
			 * @since 0.1.0.0
			 */
			enum class WriteBehavior {
				Overwrite,         ///< Overwrite the existing file.
				RenameExisting,    ///< Rename the existing file with a _ddd suffix before the extension.
				RenameNew,         ///< Rename the new file with a _ddd suffix before the extension.
				Skip               ///< Do not write the file if it exists.
			};

			/**
			 * @brief Writes a string to a file, with configurable behavior if the file exists.
			 *
			 * This method writes the specified string @p content to the file at @p path.
			 * If the file already exists, the action taken depends on the @p behavior parameter:
			 * - WriteBehavior::Overwrite: Overwrites the existing file with the new content.
			 * - WriteBehavior::RenameExisting: Renames the existing file by appending a numeric suffix (e.g., _001, _002) before the extension, then writes the new content to the original path.
			 * - WriteBehavior::RenameNew: Writes the new content to a new file with a numeric suffix before the extension, leaving the existing file unchanged.
			 * - WriteBehavior::Skip: Skips writing and returns an empty string.
			 *
			 * The function returns the name of the file containing the written data, which may differ from @p path if renaming occurred.
			 * If the operation fails or is skipped, an empty string is returned.
			 *
			 * @param path Path of the file to write (as a string).
			 * @param content String content to write to the file.
			 * @param behavior Behavior to apply if the file already exists (default: WriteBehavior::Overwrite).
			 * @return Name of the file containing the data, or empty string on failure or if skipped.
			 * @throws std::ios_base::failure If file writing fails due to I/O errors.
			 * @note This overload converts the string path to std::filesystem::path and delegates to the corresponding method.
			 * @see writeStringToFile(const std::filesystem::path&, const std::string&, WriteBehavior)
			 * @since 0.1.0.0
			 */
			static std::filesystem::path writeStringToFile(const std::string& path, const std::string& content, WriteBehavior behavior = WriteBehavior::Overwrite);

			/**
			 * @brief Writes a string to a file, with configurable behavior if the file exists (filesystem path version).
			 *
			 * This method writes the specified string @p content to the file at @p path.
			 * If the file already exists, the action taken depends on the @p behavior parameter:
			 * - WriteBehavior::Overwrite: Overwrites the existing file with the new content.
			 * - WriteBehavior::RenameExisting: Renames the existing file by appending a numeric suffix (e.g., _001, _002) before the extension, then writes the new content to the original path.
			 * - WriteBehavior::RenameNew: Writes the new content to a new file with a numeric suffix before the extension, leaving the existing file unchanged.
			 * - WriteBehavior::Skip: Skips writing and returns an empty string.
			 *
			 * The function returns the name of the file containing the written data, which may differ from @p path if renaming occurred.
			 * If the operation fails or is skipped, an empty string is returned.
			 *
			 * @param path Path of the file to write (as std::filesystem::path).
			 * @param content String content to write to the file.
			 * @param behavior Behavior to apply if the file already exists (default: WriteBehavior::Overwrite).
			 * @return Name of the file containing the data, or empty string on failure or if skipped.
			 * @throws std::ios_base::failure If file writing fails due to I/O errors.
			 * @see writeStringToFile(const std::string&, const std::string&, WriteBehavior)
			 * @since 0.1.0.0
			 */
			static std::filesystem::path writeStringToFile(const std::filesystem::path& path, const std::string& content, WriteBehavior behavior = WriteBehavior::Overwrite);

			/**
				 * @brief Writes the provided string content to the given output file stream.
				 * @details
				 * This operation writes the entire @p content to the output file stream @p ofs.
				 * It robustly handles all exceptions that may be thrown during the write operation,
				 * including but not limited to:
				 * - std::ios_base::failure: Thrown if a stream error occurs (e.g., disk full, permission denied, file not open).
				 * - std::bad_alloc: Thrown if memory allocation fails during the operation.
				 * - Any other standard or implementation-defined exceptions.
				 *
				 * The function sets the exception mask on the stream to ensure exceptions are thrown
				 * for failbit and badbit conditions, and catches all exceptions, providing detailed error reporting.
				 *
				 * @param ofs Reference to an open std::ofstream.
				 * @param content String content to write.
				 * @return true if the write succeeds, false otherwise.
				 * @note The stream must be open and ready for writing.
				 * @since 0.1.0.0
				 */
			static bool robustWrite(std::ofstream& ofs, const std::string& content);

			/**
			 * @brief Reads the entire content of a file into a string.
			 * @param path Path of the file to read.
			 * @return String containing the file content. Returns empty string if file cannot be read.
			 * @since 0.1.0.0
			 */
			static std::string readStringFromFile(const std::string& path);
			static std::shared_ptr<std::vector<std::byte>> readByteFromFile(const std::string& path);

			/**
			 * @brief Lists all regular files in the specified directory (non-recursive, cross-platform, robust).
			 *
			 * This method lists all regular files (not folders or special entries) in the directory at @p folderPath.
			 * - Uses std::filesystem for cross-platform compatibility and exception safety.
			 * - Returns only file names (not full paths), excluding folders and special entries.
			 * - If @p folderPath does not exist, is not a directory, or is inaccessible, returns an empty vector.
			 * - Handles both relative and absolute paths.
			 * - Catches and ignores all filesystem exceptions.
			 *
			 * @param folderPath Path to the directory (relative or absolute).
			 * @return Vector of file names (not including folders or special entries).
			 * @note Uses std::filesystem::directory_iterator and is_regular_file.
			 * @since 0.1.0.0
			 */
			static std::vector<std::string> listFiles(const std::string& folderPath);

			/**  @brief Removes a file by path string.
			@details
			Validates the input path, checks existence and type, and attempts removal.
			Returns a pair: success flag and error message (if any).
			@param filePath File path as std::string.
			@return std::pair<bool, std::string> (success, error message).
			@since 0.1.0
			*/
			static std::pair<bool, std::string> removeFile(const std::string& filePath);

			/**
			 * @brief Removes a file by std::filesystem::path.
			 * @details
			 * Checks for empty path, existence, and file type. Attempts removal and handles all exceptions.
			 * Returns a pair: success flag and error message (if any).
			 * @param filePath File path as std::filesystem::path.
			 * @return std::pair<bool, std::string> (success, error message).
			 * @since 0.1.0
			 * @see std::filesystem::remove
			 * @see FileHandler::removeFile(const std::string&)
			 */
			static std::pair<bool, std::string> removeFile(const std::filesystem::path& filePath);

			/**
			 * @brief Specifies behavior when destination file already exists.
			 * @details
			 * - FailIfExists: Do not overwrite existing file (default).
			 * - OverwriteIfExists: Overwrite existing file.
			 * - RenameExisting: Rename the existing file before copying.
			 * - RenameNew: Rename the new file to avoid overwriting the existing file.
			 * @since 0.1.0.0
			 */
			enum class CopyBehavior {
				FailIfExists,         ///< Do not overwrite existing file (default).
				OverwriteIfExists,    ///< Overwrite existing file.
				RenameExisting,       ///< Rename the existing file before copying.
				RenameNew             ///< Rename the new file to avoid overwriting the existing file.
			};

			/**
			 * @brief Copies a file to a new location with configurable behavior if the destination file exists.
			 *
			 * This method attempts to copy the file specified by @p sourcePath to @p destPath.
			 * The behavior when the destination file already exists is controlled by the @p behavior parameter:
			 * - @ref CopyBehavior::FailIfExists "FailIfExists": The copy operation fails if the destination file exists (default).
			 * - @ref CopyBehavior::OverwriteIfExists "OverwriteIfExists": The destination file is overwritten if it exists.
			 * - @ref CopyBehavior::RenameExisting "RenameExisting": The existing destination file is renamed by appending a suffix (_001, _002, ...) before the extension, then the copy proceeds to the original destination path.
			 * - @ref CopyBehavior::RenameNew "RenameNew": The new file is copied with a suffix (_001, _002, ...) before the extension, leaving the existing file unchanged.
			 *
			 * The method returns a pair:
			 * - first: true if the copy succeeded, false otherwise.
			 * - second: an error message if the copy failed, or an empty string on success.
			 *
			 * If renaming occurs, the suffix (_ddd) is added before the file extension, where ddd is a three-digit incremental number (e.g., file_001.txt).
			 *
			 * @param sourcePath Path to the source file to copy.
			 * @param destPath Path to the destination file.
			 * @param behavior Behavior if the destination file exists (default: @ref CopyBehavior::FailIfExists "FailIfExists").
			 * @return std::pair<bool, std::string> where first is the success flag and second is the error message.
			 * @note Uses std::filesystem::copy_file. Exceptions are caught and reported in the error message.
			 * @since 0.1.0.0
			 */

			static std::pair<bool, std::string> copyFile(const std::string& sourcePath, const std::string& destPath, CopyBehavior behavior = CopyBehavior::FailIfExists);

			/**
			 * @brief Moves (renames) a file, with configurable behavior if the destination file exists.
			 *
			 * This method moves (renames) the file at @p sourcePath to @p destPath.
			 * The behavior when the destination file already exists is controlled by @p behavior:
			 * - @ref WriteBehavior::Overwrite "Overwrite": Overwrites the existing destination file.
			 * - @ref WriteBehavior::RenameExisting "RenameExisting": Renames the existing destination file by appending a suffix (_001, _002, ...) before the extension, then moves the source file to the original destination path.
			 * - @ref WriteBehavior::RenameNew "RenameNew": Renames the new file by appending a suffix (_001, _002, ...) before the extension, leaving the existing file unchanged.
			 * - @ref WriteBehavior::Skip "Skip": Does not move the file and returns an error message.
			 *
			 * The method returns a pair:
			 * - first: true if the move succeeded, false otherwise.
			 * - second: an error message if the move failed, or an empty string on success.
			 *
			 * @param sourcePath Source file path.
			 * @param destPath Destination file path.
			 * @param behavior Behavior if the destination file exists (default: @ref WriteBehavior::Overwrite "Overwrite").
			 * @return std::pair<bool, std::string> where first is the success flag and second is the error message.
			 * @since 0.1.0.0
			 */
			static std::pair<bool, std::string> moveFile(const std::string& sourcePath, const std::string& destPath, WriteBehavior behavior = WriteBehavior::Overwrite);

			/**
			 * @brief Gets the size of a file in bytes.
			 *
			 * This method returns the size of the file at @p filePath in bytes.
			 * Uses std::filesystem for improved reliability and cross-platform support.
			 * If the file does not exist or is not a regular file, returns -1.
			 *
			 * @param filePath Path to the file.
			 * @return File size in bytes, or -1 if not accessible or not a regular file.
			 * @since 0.1.0.0
			 */
			static unsigned long long getFileSize(const std::string& filePath);

			static unsigned long long getFileSize(const std::filesystem::path& filePath);

			static unsigned long long getFileSize(const std::filesystem::path& filePath, bool check);

			/**
			 * @brief Gets the current working directory as a std::filesystem::path.
			 *
			 * This static method returns the absolute path of the process's current working directory.
			 * It uses std::filesystem::current_path() to obtain the value, which reflects the directory
			 * from which the application was launched or the last directory set via std::filesystem::current_path().
			 *
			 * @return The current working directory as a std::filesystem::path object.
			 *
			 * @see https://en.cppreference.com/w/cpp/filesystem/current_path
			 * @see https://en.cppreference.com/w/cpp/filesystem/path
			 * @since 0.1.0.0
			 *
			 * @par Example
			 * @code
			 * auto cwd = TK4CPP::Utility::FileHandler::getCurrentPath();
			 * std::cout << "Current directory: " << cwd << std::endl;
			 * @endcode
			 */
			static std::filesystem::path getCurrentPath();

			static std::filesystem::path findChild(const std::filesystem::path& parent, const std::string& childName, bool recursive);
		};
	}

}