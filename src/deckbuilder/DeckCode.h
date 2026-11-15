#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace DeckCode {
using Deck = std::map<std::uint32_t, std::uint32_t>;
inline constexpr unsigned MaxCards = 50;
inline constexpr char Prefix[] = "WSDeckImporter/1:";

std::string encode(const Deck &deck, std::string &error);
bool decode(const std::string &code, Deck &output, std::string &error);

} // namespace DeckCode