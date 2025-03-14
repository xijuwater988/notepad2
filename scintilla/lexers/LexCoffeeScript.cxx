// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for CoffeeScript, based on LexJavaScript and LexPython.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>
#include <vector>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "WordList.h"
#include "LexAccessor.h"
#include "Accessor.h"
#include "StyleContext.h"
#include "CharacterSet.h"
#include "StringUtils.h"
#include "LexerModule.h"
#include "LexerUtils.h"

using namespace Lexilla;

namespace {

// https://coffeescript.org/annotated-source/lexer.html#section-107
struct EscapeSequence {
	int outerState = SCE_COFFEESCRIPT_DEFAULT;
	int digitsLeft = 0;
	bool brace = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		brace = false;
		digitsLeft = (chNext == 'x')? 3 : ((chNext == 'u') ? 5 : 1);
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsHexDigit(ch);
	}
};

constexpr bool IsJsIdentifierStart(int ch) noexcept {
	return IsIdentifierStartEx(ch) || ch == '$';
}

constexpr bool IsJsIdentifierChar(int ch) noexcept {
	return IsIdentifierCharEx(ch) || ch == '$';
}

constexpr bool IsInterpolatedString(int state) noexcept {
	return state == SCE_COFFEESCRIPT_STRING_DQ || state == SCE_COFFEESCRIPT_XML_STRING_DQ || state == SCE_COFFEESCRIPT_TRIPLE_STRING_DQ;
}

constexpr int GetStringQuote(int state) noexcept {
	return (state < SCE_COFFEESCRIPT_STRING_DQ) ? '\'' : ((state < SCE_COFFEESCRIPT_BACKTICKS) ? '\"' : '`');
}

constexpr bool IsTripleString(int state) noexcept {
	return state == SCE_COFFEESCRIPT_TRIPLE_STRING_SQ || state == SCE_COFFEESCRIPT_TRIPLE_STRING_DQ || state == SCE_COFFEESCRIPT_TRIPLE_BACKTICKS;
}

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_COFFEESCRIPT_TASKMARKER;
}

constexpr bool FollowExpression(int chPrevNonWhite, int stylePrevNonWhite) noexcept {
	return chPrevNonWhite == ')' || chPrevNonWhite == ']'
		|| (stylePrevNonWhite >= SCE_COFFEESCRIPT_OPERATOR_PF && stylePrevNonWhite < SCE_COFFEESCRIPT_WORD)
		|| IsJsIdentifierChar(chPrevNonWhite);
}

inline bool IsRegexStart(const StyleContext &sc, int chPrevNonWhite, int stylePrevNonWhite) noexcept {
	if (IsEOLChar(sc.chNext)) {
		return false;
	}
	if (stylePrevNonWhite == SCE_COFFEESCRIPT_WORD) {
		return true;
	}
	if (FollowExpression(chPrevNonWhite, stylePrevNonWhite)) {
		// TODO: improve regex detection
		const int chNext = sc.GetLineNextChar(true);
		return !(chNext == '(' || chNext == '-' || chNext == '+' || chNext == '=' || IsJsIdentifierChar(chNext));
	}
	return true;
}

constexpr bool IsJsxTagStart(int chNext) noexcept {
	return IsJsIdentifierStart(chNext) || chNext == '>' || chNext == '{';
}

constexpr bool IsMultilineStyle(int style) noexcept {
	return style == SCE_COFFEESCRIPT_REGEX_COMMENT
		|| (style >= SCE_COFFEESCRIPT_STRING_SQ && style <= SCE_COFFEESCRIPT_TRIPLE_REGEX);
}

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_ReservedWord = 1,
	KeywordIndex_Directive = 2,
	KeywordIndex_Class = 3,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

void ColouriseCoffeeScriptDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	bool insideRegexRange = false; // inside regex character range []
	int visibleChars = 0;
	int prevIndentCount = 0;
	int indentCount = 0;
	bool prevLineContinuation = false;
	bool lineContinuation = false;
	int lineState = 0;

	int chPrevNonWhite = 0;
	int stylePrevNonWhite = SCE_COFFEESCRIPT_DEFAULT;

	EscapeSequence escSeq;
	std::vector<int> nestedState;
	int jsxTagLevel = 0;
	std::vector<int> jsxTagLevels;// nested JSX tag in expression

	if (startPos != 0) {
		// backtrack to the line starts JSX or interpolation for better coloring on typing.
		BacktrackToStart(styler, PyLineStateStringInterpolation, startPos, lengthDoc, initStyle);
	}

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (sc.currentLine > 0) {
		prevLineContinuation = (styler.GetLineState(sc.currentLine - 2) & PyLineStateLineContinuation) != 0;
		lineState = styler.GetLineState(sc.currentLine - 1);
		prevIndentCount = lineState >> 16;
		lineContinuation= (lineState & PyLineStateLineContinuation) != 0;
		lineState = 0;
	}
	if (startPos != 0 && IsSpaceEquiv(initStyle)) {
		// look back for better regex colouring
		LookbackNonWhite(styler, startPos, SCE_COFFEESCRIPT_TASKMARKER, chPrevNonWhite, stylePrevNonWhite);
	}

	while (sc.More()) {
		switch (sc.state) {
		case SCE_COFFEESCRIPT_OPERATOR:
		case SCE_COFFEESCRIPT_OPERATOR2:
		case SCE_COFFEESCRIPT_OPERATOR_PF:
			sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			break;

		case SCE_COFFEESCRIPT_NUMBER:
			if (!IsDecimalNumber(sc.chPrev, sc.ch, sc.chNext) || (sc.ch == '.' && IsJsIdentifierStart(sc.chNext))) {
				sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			}
			break;

		case SCE_COFFEESCRIPT_IDENTIFIER:
		case SCE_COFFEESCRIPT_PROPERTY_AT:
			if (!IsJsIdentifierChar(sc.ch)) {
				if (sc.state == SCE_COFFEESCRIPT_IDENTIFIER) {
					char s[128];
					sc.GetCurrent(s, sizeof(s));
					if (keywordLists[KeywordIndex_Keyword].InList(s)) {
						sc.ChangeState(SCE_COFFEESCRIPT_WORD);
					} else if (keywordLists[KeywordIndex_ReservedWord].InList(s)) {
						sc.ChangeState(SCE_COFFEESCRIPT_WORD2);
					} else if (keywordLists[KeywordIndex_Directive].InList(s)) {
						sc.ChangeState(SCE_COFFEESCRIPT_DIRECTIVE);
					} else if (sc.Match(':', ':') || keywordLists[KeywordIndex_Class].InList(s)) {
						sc.ChangeState(SCE_COFFEESCRIPT_CLASS);
					} else {
						const int chNext = sc.GetLineNextChar();
						if (chNext == ':') {
							sc.ChangeState(SCE_COFFEESCRIPT_PROPERTY);
						}
					}
					stylePrevNonWhite = sc.state;
				}
				sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			}
			break;

		case SCE_COFFEESCRIPT_XML_TAG:
		case SCE_COFFEESCRIPT_XML_ATTRIBUTE:
			if (sc.ch == '.' || sc.ch == ':') {
				const int state = sc.state;
				sc.SetState(SCE_COFFEESCRIPT_OPERATOR2);
				sc.ForwardSetState(state);
			}
			if (!(IsJsIdentifierChar(sc.ch) || sc.ch == '-')) {
				sc.SetState(SCE_COFFEESCRIPT_XML_OTHER);
				continue;
			}
			break;

		case SCE_COFFEESCRIPT_STRING_SQ:
		case SCE_COFFEESCRIPT_XML_STRING_SQ:
		case SCE_COFFEESCRIPT_TRIPLE_STRING_SQ:
		case SCE_COFFEESCRIPT_STRING_DQ:
		case SCE_COFFEESCRIPT_XML_STRING_DQ:
		case SCE_COFFEESCRIPT_TRIPLE_STRING_DQ:
		case SCE_COFFEESCRIPT_BACKTICKS:
		case SCE_COFFEESCRIPT_TRIPLE_BACKTICKS:
			if (sc.ch == '\\') {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_COFFEESCRIPT_ESCAPECHAR);
					sc.Forward();
					if (sc.Match('u', '{')) {
						escSeq.brace = true;
						escSeq.digitsLeft = 9; // Unicode code point
						sc.Forward();
					}
				}
			} else if (sc.ch == GetStringQuote(sc.state) && (!IsTripleString(sc.state) || sc.MatchNext())) {
				if (IsTripleString(sc.state)) {
					sc.Advance(2);
				}
				sc.ForwardSetState((sc.state == SCE_COFFEESCRIPT_XML_STRING_SQ || sc.state == SCE_COFFEESCRIPT_XML_STRING_DQ) ? SCE_COFFEESCRIPT_XML_OTHER : SCE_COFFEESCRIPT_DEFAULT);
				continue;
			} else if (sc.Match('#', '{') && IsInterpolatedString(sc.state)) {
				nestedState.push_back(sc.state);
				sc.ForwardSetState(SCE_COFFEESCRIPT_OPERATOR2);
			}
			break;

		case SCE_COFFEESCRIPT_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				if (escSeq.brace && sc.ch == '}') {
					sc.Forward();
				}
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;

		case SCE_COFFEESCRIPT_REGEX:
		case SCE_COFFEESCRIPT_TRIPLE_REGEX:
			if (sc.atLineStart && sc.state == SCE_COFFEESCRIPT_REGEX) {
				sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			} else if (sc.ch == '\\') {
				sc.Forward();
			} else if (sc.ch == '[' || sc.ch == ']') {
				insideRegexRange = sc.ch == '[';
			} else if (sc.ch == '#') {
				if (!insideRegexRange && sc.state == SCE_COFFEESCRIPT_TRIPLE_REGEX) {
					if (sc.chNext == '{') {
						nestedState.push_back(sc.state);
						sc.ForwardSetState(SCE_COFFEESCRIPT_OPERATOR2);
					} else {
						sc.SetState(SCE_COFFEESCRIPT_REGEX_COMMENT);
					}
				}
			} else if (sc.ch == '/' && !insideRegexRange && (sc.state != SCE_COFFEESCRIPT_TRIPLE_REGEX || sc.MatchNext('/', '/'))) {
				if (sc.state == SCE_COFFEESCRIPT_TRIPLE_REGEX) {
					sc.Advance(2);
				}
				sc.Forward();
				// regex flags
				while (IsLowerCase(sc.ch)) {
					sc.Forward();
				}
				sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			}
			break;

		case SCE_COFFEESCRIPT_REGEX_COMMENT:
			if (sc.atLineStart) {
				sc.SetState(SCE_COFFEESCRIPT_TRIPLE_REGEX);
				continue;
			}
			break;

		case SCE_COFFEESCRIPT_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_COFFEESCRIPT_DEFAULT);
			}
			break;

		case SCE_COFFEESCRIPT_COMMENTBLOCK:
			if (sc.atLineStart) {
				lineState = PyLineStateMaskCommentLine;
			}
			if (sc.Match('#', '#', '#')) {
				sc.Advance(2);
				sc.ForwardSetState(SCE_COFFEESCRIPT_DEFAULT);
				if (lineState == PyLineStateMaskCommentLine && sc.GetLineNextChar() != '\0') {
					lineState = 0;
				}
			}
			break;

		case SCE_COFFEESCRIPT_XML_TEXT:
		case SCE_COFFEESCRIPT_XML_OTHER:
			if (sc.ch == '>' || sc.Match('/', '>')) {
				sc.SetState(SCE_COFFEESCRIPT_XML_TAG);
				if (sc.ch == '/') {
					// self closing <tag />
					--jsxTagLevel;
					sc.Forward();
				}
				chPrevNonWhite = '>';
				stylePrevNonWhite = SCE_COFFEESCRIPT_XML_TAG;
				sc.ForwardSetState((jsxTagLevel == 0) ? SCE_COFFEESCRIPT_DEFAULT : SCE_COFFEESCRIPT_XML_TEXT);
				continue;
			} else if (sc.ch == '=' && (sc.state == SCE_COFFEESCRIPT_XML_OTHER)) {
				sc.SetState(SCE_COFFEESCRIPT_OPERATOR2);
				sc.ForwardSetState(SCE_COFFEESCRIPT_XML_OTHER);
				continue;
			} else if ((sc.ch == '\'' || sc.ch == '"') && (sc.state == SCE_COFFEESCRIPT_XML_OTHER)) {
				sc.SetState((sc.ch == '\'') ? SCE_COFFEESCRIPT_XML_STRING_SQ : SCE_COFFEESCRIPT_XML_STRING_DQ);
			} else if ((sc.state == SCE_COFFEESCRIPT_XML_OTHER) && IsJsIdentifierStart(sc.ch)) {
				sc.SetState(SCE_COFFEESCRIPT_XML_ATTRIBUTE);
			} else if (sc.ch == '{') {
				jsxTagLevels.push_back(jsxTagLevel);
				nestedState.push_back(sc.state);
				sc.SetState(SCE_COFFEESCRIPT_OPERATOR2);
				jsxTagLevel = 0;
			} else if (sc.Match('<', '/')) {
				--jsxTagLevel;
				sc.SetState(SCE_COFFEESCRIPT_XML_TAG);
				sc.Forward();
			} else if (sc.ch == '<') {
				++jsxTagLevel;
				sc.SetState(SCE_COFFEESCRIPT_XML_TAG);
			}
			break;
		}

		if (sc.state == SCE_COFFEESCRIPT_DEFAULT) {
			if (sc.ch == '#') {
				if (visibleChars == 0) {
					lineState = PyLineStateMaskCommentLine;
				}
				if (sc.MatchNext('#', '#')) {
					sc.SetState(SCE_COFFEESCRIPT_COMMENTBLOCK);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_COFFEESCRIPT_COMMENTLINE);
				}
			} else if (sc.ch == '\'') {
				if (sc.MatchNext('\'', '\'')) {
					sc.SetState(SCE_COFFEESCRIPT_TRIPLE_STRING_SQ);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_COFFEESCRIPT_STRING_SQ);
				}
			} else if (sc.ch == '\"') {
				if (sc.MatchNext('\"', '\"')) {
					sc.SetState(SCE_COFFEESCRIPT_TRIPLE_STRING_DQ);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_COFFEESCRIPT_STRING_DQ);
				}
			} else if (sc.ch == '`') {
				if (sc.MatchNext('`', '`')) {
					sc.SetState(SCE_COFFEESCRIPT_TRIPLE_BACKTICKS);
					sc.Advance(2);
				} else {
					sc.SetState(SCE_COFFEESCRIPT_BACKTICKS);
				}
			} else if (IsNumberStartEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_COFFEESCRIPT_NUMBER);
			} else if (sc.ch == '@' && IsJsIdentifierStart(sc.chNext)) {
				sc.SetState(SCE_COFFEESCRIPT_PROPERTY_AT);
			} else if (IsJsIdentifierStart(sc.ch)) {
				sc.SetState(SCE_COFFEESCRIPT_IDENTIFIER);
			} else if (sc.ch == '/') {
				sc.SetState(SCE_COFFEESCRIPT_OPERATOR);
				if (sc.chNext == '/') {
					sc.Forward();
					if (sc.chNext == '/') {
						insideRegexRange = false;
						sc.ChangeState(SCE_COFFEESCRIPT_TRIPLE_REGEX);
						sc.Forward();
					}
				} else if (IsRegexStart(sc, chPrevNonWhite, stylePrevNonWhite)) {
					insideRegexRange = false;
					sc.ChangeState(SCE_COFFEESCRIPT_REGEX);
				}
			} else if (sc.ch == '+' || sc.ch == '-') {
				if (sc.ch == sc.chNext) {
					// highlight ++ and -- as different style to simplify regex detection.
					sc.SetState(SCE_COFFEESCRIPT_OPERATOR_PF);
					sc.Forward();
				} else {
					sc.SetState(SCE_COFFEESCRIPT_OPERATOR);
				}
			} else if (sc.ch == '<') {
				// <tag></tag>
				if (sc.chNext == '/') {
					--jsxTagLevel;
					sc.SetState(SCE_COFFEESCRIPT_XML_TAG);
					sc.Forward();
				} else if (IsJsxTagStart(sc.chNext)) {
					++jsxTagLevel;
					sc.SetState(SCE_COFFEESCRIPT_XML_TAG);
				} else {
					sc.SetState(SCE_COFFEESCRIPT_OPERATOR);
				}
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_COFFEESCRIPT_OPERATOR);
				if (!nestedState.empty()) {
					if (sc.ch == '{') {
						nestedState.push_back(SCE_COFFEESCRIPT_DEFAULT);
						jsxTagLevels.push_back(jsxTagLevel);
						jsxTagLevel = 0;
					} else if (sc.ch == '}') {
						jsxTagLevel = TryTakeAndPop(jsxTagLevels);
						const int outerState = TakeAndPop(nestedState);
						if (outerState != SCE_COFFEESCRIPT_DEFAULT) {
							sc.ChangeState(SCE_COFFEESCRIPT_OPERATOR2);
						}
						sc.ForwardSetState(outerState);
						continue;
					}
				} else if (visibleChars == 0 && (sc.ch == '}' || sc.ch == ']' || sc.ch == ')')) {
					lineState |= PyLineStateMaskCloseBrace;
				}
			}
		}

		if (visibleChars == 0 && IsASpaceOrTab(sc.ch)) {
			++indentCount;
		}
		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
				stylePrevNonWhite = sc.state;
			}
		}
		if (sc.atLineEnd) {
			if (lineContinuation) {
				indentCount = prevIndentCount;
				if (!prevLineContinuation) {
					++indentCount;
				}
			}
			lineState |= (indentCount << 16);
			prevIndentCount = indentCount;
			prevLineContinuation = lineContinuation;
			if (sc.state != SCE_COFFEESCRIPT_COMMENTLINE && sc.LineEndsWith('\\')) {
				lineContinuation = true;
				lineState |= PyLineStateLineContinuation;
			} else {
				lineContinuation = false;
			}
			if (!nestedState.empty() || !(jsxTagLevel == 0 && jsxTagLevels.empty())) {
				lineState |= PyLineStateStringInterpolation | PyLineStateMaskTripleQuote;
			} else if (IsMultilineStyle(sc.state)) {
				lineState |= PyLineStateMaskTripleQuote;
			} else if (visibleChars == 0 && (lineState & PyLineStateMaskCommentLine) == 0) {
				lineState |= PyLineStateMaskEmptyLine;
			}
			styler.SetLineState(sc.currentLine, lineState);
			lineState = 0;
			insideRegexRange = false;
			visibleChars = 0;
			indentCount = 0;
		}
		sc.Forward();
	}
}

}

LexerModule lmCoffeeScript(SCLEX_COFFEESCRIPT, ColouriseCoffeeScriptDoc, "coffeescript", FoldPyDoc);
