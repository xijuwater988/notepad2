// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// most system already has an easy way to input Emoji, disable this to reduce binary size.
#define NP2_ENABLE_LATEX_LIKE_EMOJI_INPUT	1

//++Autogenerated -- start of section automatically generated
// LaTeX input sequences based on Julia version 1.10.0-DEV.107 (Wednesday 7 December 2022),
// documented at https://docs.julialang.org/en/v1.10-dev/manual/unicode-input/.
// Emoji input sequences based on https://github.com/iamcal/emoji-data/blob/master/emoji_pretty.json,
// downloaded on Wednesday 07 December 2022.

enum {
	MinLaTeXInputSequenceLength = 1,
	MaxLaTeXInputSequenceLength = 25,

#if NP2_ENABLE_LATEX_LIKE_EMOJI_INPUT
	EmojiInputSequencePrefixLength = 1,
	EmojiInputSequenceSuffixLength = 1,
	MinEmojiInputSequenceLength = 1 + EmojiInputSequencePrefixLength, // suffix is optional
	MaxEmojiInputSequenceLength = 54 + EmojiInputSequencePrefixLength + EmojiInputSequenceSuffixLength,

	MaxLaTeXInputBufferLength = 1 + MaxEmojiInputSequenceLength + 1,
#else
	MaxLaTeXInputBufferLength = 1 + MaxLaTeXInputSequenceLength + 1,
#endif
};

static inline bool IsLaTeXInputSequenceChar(char ch) {
	return (ch >= 'a' && ch <= 'z')
		|| (ch >= 'A' && ch <= 'Z')
		|| (ch >= '0' && ch <= '9')
		|| ch == '!' || ch == '(' || ch == ')' || ch == '+' || ch == '-'
		|| ch == '/' || ch == '=' || ch == '^' || ch == '_'
#if NP2_ENABLE_LATEX_LIKE_EMOJI_INPUT
		|| ch == ':'
#endif
	;
}
//--Autogenerated -- end of section automatically generated


/// all LaTeX input sequences excludes the prefix '\', separated by space (U+0020).
extern const char * const LaTeXInputSequenceString;

#if NP2_ENABLE_LATEX_LIKE_EMOJI_INPUT
/// all Emoji input sequences excludes the prefix '\:' and suffix ':', separated by space (U+0020).
extern const char * const EmojiInputSequenceString;
#endif

/*!
 * @brief Get Unicode UTF-16 characters for LaTeX or Emoji input sequence.
 * example: \sum to U+2211 ∑, \:smile: to U+1F604 😄 and \gvertneqq to U+2269 + U+FE00 ≩︀.
 * @param sequence The input sequence withou the prefix '\'.
 * sequence[0] == ':' indicates Emoji, the suffix ':' is optional (but must be counted into length when included).
 * @param length Length for the input sequence withou the prefix '\'.
 * @return Returns the corresponding Unicode characters or zero when the input sequence is not found.
 */
uint32_t GetLaTeXInputUnicodeCharacter(const char *sequence, size_t length);

#ifdef __cplusplus
}
#endif
