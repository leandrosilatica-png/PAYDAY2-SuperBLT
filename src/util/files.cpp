
#include "util/util.h"

#include <fstream>
#include <string>
#include <vector>

using namespace std;

namespace raidhook
{
	namespace Util
	{

		string GetFileContents(const string& filename)
		{
			ifstream file(filename, std::ios::binary | std::ios::ate);
			if (!file)
				return {};

			std::streamoff end = file.tellg();
			if (end <= 0)
				return {};

			string str(static_cast<size_t>(end), '\0');
			file.seekg(0, std::ios::beg);
			file.read(str.data(), static_cast<std::streamsize>(str.size()));
			str.resize(static_cast<size_t>(file.gcount()));

			return str;
		}

		// FIXME this function really should return a boolean, if it succeeded
		void EnsurePathWritable(const std::string& path)
		{
			size_t finalSlash = path.find_last_of("/\\");
			if (finalSlash == std::string::npos)
				return;
			std::string finalPath = path.substr(0, finalSlash);
			if (DirectoryExists(finalPath))
				return;
			CreateDirectoryPath(finalPath);
		}

		bool RemoveFilesAndDirectory(const std::string& path)
		{
			std::vector<std::string> dirs = raidhook::Util::GetDirectoryContents(path, true);
			std::vector<std::string> files = raidhook::Util::GetDirectoryContents(path);
			bool failed = false;

			for (auto it = files.begin(); it < files.end(); it++)
			{
				failed = remove((path + "/" + *it).c_str());
				if (failed)
					return false;
			}
			for (auto it = dirs.begin(); it < dirs.end(); it++)
			{
				if (*it == "." || *it == "..")
					continue;

				// dont follow symlinks, just delete them as a file, recurse on normal directories
				if (raidhook::Util::IsSymlink(path + "/" + *it))
				{
					failed = remove((path + "/" + *it).c_str());
				}
				else
				{
					failed = raidhook::Util::RemoveFilesAndDirectory(path + "/" + *it) == 0;
				}
				if (failed)
					return false;
			}
			return RemoveEmptyDirectory(path);
		}

		bool CreateDirectoryPath(const std::string& path)
		{
			std::string newPath;
			newPath.reserve(path.size() + 1);
			std::vector<std::string> paths = Util::SplitString(path, '/');
			for (const auto& i : paths)
			{
				newPath.append(i).push_back('/');
				CreateDirectorySingle(newPath);
			}
			return true;
		}

		void SplitString(const std::string& s, char delim, std::vector<std::string>& elems)
		{
			size_t start = 0;
			while (start <= s.size())
			{
				size_t end = s.find(delim, start);
				if (end == std::string::npos)
					end = s.size();
				if (end != start)
					elems.emplace_back(s, start, end - start);
				if (end == s.size())
					break;
				start = end + 1;
			}
		}

		std::vector<std::string> SplitString(const std::string& s, char delim)
		{
			std::vector<std::string> elems;
			SplitString(s, delim, elems);
			return elems;
		}

	} // namespace Util
} // namespace raidhook
