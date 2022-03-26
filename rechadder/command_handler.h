#pragma once
#include "command.h"
struct command_handler
{
	std::vector<command> commands{};

	static std::vector<std::string> split(const std::string& s, char seperator)
	{
		std::vector<std::string> output;
		std::string::size_type prev_pos = 0, pos = 0;
		while ((pos = s.find(seperator, pos)) != std::string::npos)
		{
			std::string substring(s.substr(prev_pos, pos - prev_pos));
			output.push_back(substring);
			prev_pos = ++pos;
		}
		output.push_back(s.substr(prev_pos, pos - prev_pos));
		return output;
	}

	bool to_handler(const std::string& message, const std::string& sender)
	{
		// find possible commands that match.
		auto tokens = split(message, ' ');
		if (tokens.empty()) return true;
		for (const auto& cmd : commands)
		{
			if (tokens.at(0) == cmd.name)
			{
				tokens.erase(tokens.begin());
				return cmd.action(tokens, sender);
			}
		}
		return true;
	}
};

