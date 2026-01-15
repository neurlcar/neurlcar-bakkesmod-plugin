#include "pch.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include "csvparser.h"

/*
Reads a CSV of numeric values and returns column-major data:
result[col][row]. Optionally skips the first line as a header.
*/
std::vector<std::vector<double>> csvparser(const std::filesystem::path& filename, bool hasHeader)
{
	std::vector<std::vector<double>> parsedCSV;
	std::ifstream ifs(filename);

    if (ifs.is_open()) {
        std::string line;
        if (hasHeader)
        {
            std::getline(ifs, line);
        }
        while (std::getline(ifs, line))
        {
            std::istringstream ss(line);
            std::string token;

            size_t colIndex = 0;
            while (std::getline(ss, token, ','))
            {
                double value = std::stod(token);

                if (parsedCSV.size() <= colIndex)
                {
                    parsedCSV.emplace_back();
                }

                parsedCSV[colIndex].push_back(value);
                ++colIndex;
            }
        }
        ifs.close();
    }

	LOG("csv size is {}", std::to_string(parsedCSV.size()));


	ifs.close();

	return parsedCSV;
}