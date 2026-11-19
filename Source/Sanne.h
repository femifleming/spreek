// Sanne — the pixel tutor from the Tongval mockups (option B), drawn on the OLED instead of the robot.
// 36x42 grey levels (hex 0..f, 0 = pixel off). Generated from ~/tongval/mockups/sanne.html.
#pragma once
#include <string>

namespace sm::sanne {

constexpr int W = 36, H = 42, mouthX = 18, mouthY = 26;

inline const char* const eyesOpen[H] = {
    "00000000000000000aa00000000000000000",
    "0000000000000aaaaaaaaaa0000000000000",
    "00000000000aaaaaaaaaaaaaa00000000000",
    "0000000000aaaaaaaaaaaaaaaaa000000000",
    "00000000aaaaaaaaaaaaaaaaaaaa00000000",
    "00000000aaaaaaaaaaaaaaaaaaaaa0000000",
    "0000000aaaaaaaaaaaaaaaaaaaaaa0000000",
    "000000aaaaaaaaaaaaaaaaaaaaaaaa000000",
    "000000aaaaaaaaaaaaaaaaaaaaaaaa000000",
    "00000aaaaaaaaaaaaaaaaaafaaaaaaa00000",
    "00000aaaaaaaaaaaaaaaaafffaaaaaa00000",
    "00000aaaaaaaaaaaaaaafffffaaaaaa00000",
    "00000aaaaaaaaaaaaaffffffffaaaaaa0000",
    "0000aaaaaaaffffffffffffffffaaaaa0000",
    "0000aaaaaaffa66affffa66afffaaaaa0000",
    "0000aaaaafffaffffffffffaffffaaaa0000",
    "0000aaaafffffaffffffffafffffaaaa0000",
    "0000aaaaffffa16ffffff61affffaaaa0000",
    "0000aaafffffa16ffffff61afffffaaa0000",
    "0000aaaffffffaffffffffaffffffaaa0000",
    "0000aaaffffffffffafffffffffffaaa0000",
    "0000aaaafffffffffaffffffffffaaaa0000",
    "0000aaaaffffffffffffffffffffaaaa0000",
    "0000aaaa0ffffffffffffffffff0aaaa0000",
    "0000aaaa0ffffffffffffffffff0aaaa0000",
    "00000aaa0ffffffffffffffffff0aaa00000",
    "00000aaa00ffffffffffffffff00aaa00000",
    "00000aaa00ffffffffffffffff0aaaa00000",
    "00000aaaa00ffffffffffffff00aaaa00000",
    "00000aaaa000ffffffffffff000aaaa00000",
    "00000aaaa0000ffffffffff0000aaaa00000",
    "00000aaaa00000ffffffff00000aaaa00000",
    "000000000000000ffffff000000000000000",
    "000000000000000ffffff000000000000000",
    "000000000000000ffffff000000000000000",
    "00000000000000affffffa00000000000000",
    "00000000000aaaaa0000aaaaa00000000000",
    "000000000fffaaaaa00aaaaafff000000000",
    "00000000ffffaaaaa00aaaaaffff00000000",
    "000000fffffffaaaaaaaaaafffffff000000",
    "00000fffffffffaaaffaaafffffffff00000",
    "0000ffffffffffaaaffaaaffffffffff0000" };

inline const char* const eyesClosed[H] = {
    "00000000000000000aa00000000000000000",
    "0000000000000aaaaaaaaaa0000000000000",
    "00000000000aaaaaaaaaaaaaa00000000000",
    "0000000000aaaaaaaaaaaaaaaaa000000000",
    "00000000aaaaaaaaaaaaaaaaaaaa00000000",
    "00000000aaaaaaaaaaaaaaaaaaaaa0000000",
    "0000000aaaaaaaaaaaaaaaaaaaaaa0000000",
    "000000aaaaaaaaaaaaaaaaaaaaaaaa000000",
    "000000aaaaaaaaaaaaaaaaaaaaaaaa000000",
    "00000aaaaaaaaaaaaaaaaaafaaaaaaa00000",
    "00000aaaaaaaaaaaaaaaaafffaaaaaa00000",
    "00000aaaaaaaaaaaaaaafffffaaaaaa00000",
    "00000aaaaaaaaaaaaaffffffffaaaaaa0000",
    "0000aaaaaaaffffffffffffffffaaaaa0000",
    "0000aaaaaaffa66affffa66afffaaaaa0000",
    "0000aaaaafffaffffffffffaffffaaaa0000",
    "0000aaaaffffffffffffffffffffaaaa0000",
    "0000aaaaffffffffffffffffffffaaaa0000",
    "0000aaafffffa66ffffff66afffffaaa0000",
    "0000aaaffffffffffffffffffffffaaa0000",
    "0000aaaffffffffffafffffffffffaaa0000",
    "0000aaaafffffffffaffffffffffaaaa0000",
    "0000aaaaffffffffffffffffffffaaaa0000",
    "0000aaaa0ffffffffffffffffff0aaaa0000",
    "0000aaaa0ffffffffffffffffff0aaaa0000",
    "00000aaa0ffffffffffffffffff0aaa00000",
    "00000aaa00ffffffffffffffff00aaa00000",
    "00000aaa00ffffffffffffffff0aaaa00000",
    "00000aaaa00ffffffffffffff00aaaa00000",
    "00000aaaa000ffffffffffff000aaaa00000",
    "00000aaaa0000ffffffffff0000aaaa00000",
    "00000aaaa00000ffffffff00000aaaa00000",
    "000000000000000ffffff000000000000000",
    "000000000000000ffffff000000000000000",
    "000000000000000ffffff000000000000000",
    "00000000000000affffffa00000000000000",
    "00000000000aaaaa0000aaaaa00000000000",
    "000000000fffaaaaa00aaaaafff000000000",
    "00000000ffffaaaaa00aaaaaffff00000000",
    "000000fffffffaaaaaaaaaafffffff000000",
    "00000fffffffffaaaffaaafffffffff00000",
    "0000ffffffffffaaaffaaaffffffffff0000" };

// mouth shapes: '.' keep face, 'L' lip grey, 'D' dark inside, 'T' teeth
struct Mouth { int w, h; const char* rows[6]; };
enum Viseme { Rest, AA, IE, OE, UU, EU, Mid, VisemeCount };
inline const Mouth mouths[VisemeCount] = {
    { 5, 2, { "LLLLL", ".LLL." } },                                          // rest: closed, small smile
    { 7, 6, { ".LLLLL.", "LDTTTDL", "LDDDDDL", "LDDDDDL", ".LDDDL.", "..LLL.." } }, // aa: open wide (maan)
    { 9, 3, { "LLLLLLLLL", "LDTTTTTDL", ".LLLLLLL." } },                     // ie: spread (niet)
    { 5, 4, { ".LLL.", "LDDDL", "LDDDL", ".LLL." } },                        // oe: round, small (boek)
    { 5, 3, { ".LLL.", "LLDLL", ".LLL." } },                                 // uu: tight round, pushed (muur)
    { 6, 3, { ".LLLL.", "LDDDDL", ".LLLL." } },                              // eu: round, mid (neus)
    { 5, 3, { ".LLL.", "LDDDL", ".LLL." } },                                 // other sounds: slightly open
};

inline bool has (const std::string& s, const char* t) { return s.find (t) != std::string::npos; }

// which mouth to show for an IPA phoneme id
inline Viseme visemeFor (const std::string& p)
{
    if (p.empty()) return Rest;
    if (has (p, "m") || has (p, "b") || has (p, "p")) return Rest;                       // lips closed
    if (has (p, "\xc5\x93y") || has (p, "\xc3\xb8") || has (p, "\xc5\x93") || has (p, "\xc9\x99") || has (p, "\xc9\x9c")) return EU; // œy ø œ ə ɜ
    if (has (p, "\xca\x8f") || p == "y" || p == "y\xcb\x90" || p == "w") return UU;         // ʏ y yː w
    if (has (p, "\xca\x8cu") || has (p, "a\xca\x8a") || has (p, "o\xca\x8a")) return OE;  // ʌu aʊ oʊ
    if (has (p, "\xc9\x91") || has (p, "a") || has (p, "\xc3\xa6") || has (p, "\xca\x8c") || has (p, "\xc9\x92")) return AA; // ɑ a æ ʌ ɒ
    if (has (p, "u") || has (p, "\xca\x8a") || has (p, "o") || has (p, "\xc9\x94")) return OE;         // u ʊ o ɔ
    if (has (p, "i") || has (p, "\xc9\xaa") || has (p, "e") || has (p, "\xc9\x9b")) return IE;         // i ɪ e ɛ (ɛi)
    return Mid;
}

} // namespace sm::sanne
