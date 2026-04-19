#include "Application.h"

std::vector<std::string> SplitString(const std::string &input, char delimiter)
{
    std::vector<std::string> parts;
    std::stringstream ss(input);
    std::string part;

    while (std::getline(ss, part, delimiter))
    {
        parts.push_back(part);
    }

    return parts;
}