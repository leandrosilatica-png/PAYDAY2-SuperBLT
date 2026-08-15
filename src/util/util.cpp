#include "util.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>

namespace raidhook
{
	namespace Util
	{
		namespace
		{
			class SHA256Provider
			{
			  public:
				SHA256Provider()
				{
					if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
						throw std::runtime_error("Failed to open SHA-256 algorithm provider");

					DWORD resultLength = 0;
					if (BCryptGetProperty(handle, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength),
					                      sizeof(objectLength), &resultLength, 0) != 0)
					{
						BCryptCloseAlgorithmProvider(handle, 0);
						throw std::runtime_error("Failed to get hash object length");
					}
				}

				~SHA256Provider()
				{
					BCryptCloseAlgorithmProvider(handle, 0);
				}

				BCRYPT_ALG_HANDLE handle = nullptr;
				DWORD objectLength = 0;
			};

			SHA256Provider& GetSHA256Provider()
			{
				static SHA256Provider provider;
				return provider;
			}

			class SHA256Hash
			{
			  public:
				SHA256Hash()
				{
					auto& provider = GetSHA256Provider();
					object.resize(provider.objectLength);
					if (BCryptCreateHash(provider.handle, &handle, object.data(), provider.objectLength, nullptr, 0,
					                     0) != 0)
						throw std::runtime_error("Failed to create SHA-256 hash");
				}

				~SHA256Hash()
				{
					if (handle)
						BCryptDestroyHash(handle);
				}

				void update(const void* data, size_t size)
				{
					auto* bytes = static_cast<const unsigned char*>(data);
					while (size != 0)
					{
						ULONG chunk = static_cast<ULONG>(std::min<size_t>(size, std::numeric_limits<ULONG>::max()));
						if (BCryptHashData(handle, const_cast<PUCHAR>(bytes), chunk, 0) != 0)
							throw std::runtime_error("Failed to hash data");
						bytes += chunk;
						size -= chunk;
					}
				}

				std::vector<uint8_t> finish()
				{
					std::vector<uint8_t> hash(32);
					if (BCryptFinishHash(handle, hash.data(), static_cast<ULONG>(hash.size()), 0) != 0)
						throw std::runtime_error("Failed to finish SHA-256 hash");
					return hash;
				}

			  private:
				BCRYPT_HASH_HANDLE handle = nullptr;
				std::vector<uint8_t> object;
			};

			std::string sha256_file(const std::string& filename);
		} // namespace

		Exception::Exception(const char* file, int line) : mFile(file), mLine(line)
		{
		}

		Exception::Exception(std::string msg, const char* file, int line)
			: mFile(file), mLine(line), mMsg(std::move(msg))
		{
		}

		const char* Exception::what() const throw()
		{
			if (!mMsg.empty())
			{
				return mMsg.c_str();
			}

			return std::exception::what();
		}

		const char* Exception::exceptionName() const
		{
			return "An exception";
		}

		void Exception::writeToStream(std::ostream& os) const
		{
			os << exceptionName() << " occurred @ (" << mFile << ':' << mLine << "). " << what();
		}

		// helper function to print the digest bytes as a hex string
		std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes)
		{
			static constexpr char hex[] = "0123456789abcdef";
			std::string result(bytes.size() * 2, '\0');
			for (size_t i = 0; i < bytes.size(); ++i)
			{
				result[i * 2] = hex[bytes[i] >> 4];
				result[i * 2 + 1] = hex[bytes[i] & 0xf];
			}
			return result;
		}

		// Perform SHA-256 hash using Windows CNG API
		std::string sha256(const std::string& input)
		{
			SHA256Hash hash;
			hash.update(input.data(), input.size());
			return bytes_to_hex_string(hash.finish());
		}

		namespace
		{
			std::string sha256_file(const std::string& filename)
			{
				SHA256Hash hash;
				std::ifstream file(filename, std::ios::binary);
				std::array<char, 256 * 1024> buffer;
				while (file)
				{
					file.read(buffer.data(), buffer.size());
					std::streamsize bytesRead = file.gcount();
					if (bytesRead > 0)
						hash.update(buffer.data(), static_cast<size_t>(bytesRead));
				}
				return bytes_to_hex_string(hash.finish());
			}
		} // namespace

		void RecurseDirectoryPaths(std::vector<std::string>& paths, std::string directory, bool ignore_versioning)
		{
			WIN32_FIND_DATAA entry;
			HANDLE search = FindFirstFileA((directory + "*").c_str(), &entry);
			if (search == INVALID_HANDLE_VALUE)
				RAIDHOOK_THROW_IO_MSG("FindFirstFile() failed");

			do
			{
				std::string_view name(entry.cFileName);
				bool isDirectory = (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
				if (!isDirectory)
				{
					paths.push_back(directory + entry.cFileName);
					continue;
				}

				if (name == "." || name == "..")
					continue;
				if (ignore_versioning && (name == ".hg" || name == ".git"))
					continue;
				if ((entry.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
					continue;

				RecurseDirectoryPaths(paths, directory + entry.cFileName + "/", ignore_versioning);
			} while (FindNextFileA(search, &entry));

			DWORD error = GetLastError();
			FindClose(search);
			if (error != ERROR_NO_MORE_FILES)
				RAIDHOOK_THROW_IO_MSG("FindNextFile() failed");
		}

		static bool CompareStringsCaseInsensitive(const std::string& a, const std::string& b)
		{
			return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
			                                    [](unsigned char left, unsigned char right)
			                                    { return std::tolower(left) < std::tolower(right); });
		}

		std::string GetDirectoryHash(const std::string& directory)
		{
			std::vector<std::string> paths;
			RecurseDirectoryPaths(paths, directory, true);

			// Case-insensitive sort, since that's how it was always done.
			// (on Windows, the filenames were previously downcased in RecurseDirectoryPaths, but that
			//  obviously won't work with a case-sensitive filesystem)
			// If I were to rewrite BLT from scratch I'd certainly make this case-sensitive, but there's no good
			//  way to change this without breaking hashing on previous versions.
			std::sort(paths.begin(), paths.end(), CompareStringsCaseInsensitive);

			std::string hashconcat;
			hashconcat.reserve(paths.size() * 64);

			for (const std::string& path : paths)
			{
				hashconcat += sha256_file(path);
			}

			return sha256(hashconcat);
		}

		std::string GetFileHash(const std::string& file)
		{
			// This has to be hashed twice otherwise it won't be the same hash if we're checking against a file uploaded
			// to the server
			std::string hash = sha256_file(file);
			return sha256(hash);
		}

		template <> std::string ToHex(uint64_t value)
		{
			std::stringstream ss;
			ss << std::hex << std::setw(16) << std::setfill('0') << value;
			return ss.str();
		}

		std::string GetModuleFileNameCxx(HMODULE hModule)
		{
			std::string buffer(MAX_PATH, '\0');
			DWORD len = GetModuleFileNameA(hModule, buffer.data(), buffer.size());
			// len is 0 for an error, which just makes us return an empty string.

			buffer.resize(len);
			return buffer;
		}

		std::string StripWhitespace(std::string s)
		{
			// Why on earth isn't this part of std?

			// Trim from start (left)
			s.erase(s.begin(), std::ranges::find_if(s, [](unsigned char ch) { return !std::isspace(ch); }));
			// Trim from end (right)
			s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
			        s.end());
			return s;
		}

	} // namespace Util
} // namespace raidhook
