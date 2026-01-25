#pragma once

#include <string_view>

namespace binja::rewind::core::decode {

bool is_call_mnemonic(std::string_view mnemonic, std::string_view arch_name);
bool is_return_mnemonic(std::string_view mnemonic, std::string_view arch_name);

} // namespace binja::rewind::core::decode
