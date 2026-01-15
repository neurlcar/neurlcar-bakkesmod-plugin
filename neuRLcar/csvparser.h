#pragma once

#include <vector>
#include <string>

std::vector<std::vector<double>> csvparser(const std::filesystem::path& filename, bool hasHeader = true);