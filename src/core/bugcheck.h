// What each Windows stop code means, in plain language.
#pragma once

#include <cstdint>
#include <string>

namespace why {

enum class BugCheckHint { Unknown, Driver, Graphics, Memory, Storage, Hardware };

struct BugCheckInfo {
    std::string name;  // Such as DRIVER_IRQL_NOT_LESS_OR_EQUAL. Falls back to the hex code.
    std::string plain;
    BugCheckHint hint = BugCheckHint::Unknown;
    int addressParam = -1;  // Which parameter (0-3) holds an address inside the responsible module.
};

BugCheckInfo bugCheckInfo(std::uint32_t code);

}  // namespace why
