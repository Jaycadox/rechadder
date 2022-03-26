#pragma once
#include <string>
#include <vector>
#include <functional>
struct command
{
	std::string name{};
	std::function<bool(const std::vector<std::string>&,
		const std::string&)> action{};
};

