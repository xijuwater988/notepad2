// This file is part of Notepad2.
// See License.txt for details about distribution and modification.
//! Lexer for Go.

#include <cassert>
#include <cstring>

#include <string>
#include <string_view>

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

using namespace Lexilla;

namespace {

struct EscapeSequence {
	int outerState = SCE_GO_DEFAULT;
	int digitsLeft = 0;
	bool hex = false;

	// highlight any character as escape sequence.
	bool resetEscapeState(int state, int chNext) noexcept {
		if (IsEOLChar(chNext)) {
			return false;
		}
		outerState = state;
		digitsLeft = 1;
		hex = true;
		if (chNext == 'x') {
			digitsLeft = 3;
		} else if (chNext == 'u') {
			digitsLeft = 5;
		} else if (chNext == 'U') {
			digitsLeft = 9;
		} else if (IsOctalDigit(chNext)) {
			digitsLeft = 3;
			hex = false;
		}
		return true;
	}
	bool atEscapeEnd(int ch) noexcept {
		--digitsLeft;
		return digitsLeft <= 0 || !IsOctalOrHex(ch, hex);
	}
};

//KeywordIndex++Autogenerated -- start of section automatically generated
enum {
	KeywordIndex_Keyword = 0,
	KeywordIndex_PrimitiveType = 1,
	KeywordIndex_BuiltinFunction = 2,
	KeywordIndex_Type = 3,
	KeywordIndex_Struct = 4,
	KeywordIndex_Interface = 5,
	KeywordIndex_Constant = 6,
};
//KeywordIndex--Autogenerated -- end of section automatically generated

enum class KeywordType {
	None = SCE_GO_DEFAULT,
	Type = SCE_GO_TYPE,
	Struct = SCE_GO_STRUCT,
	Interface = SCE_GO_INTERFACE,
	Constant = SCE_GO_CONSTANT,
	Identifier = SCE_GO_IDENTIFIER,
	Label = SCE_GO_LABEL,
};

enum class GoFunction {
	None = 0,
	Define,
	Caller,
	Name,
	Param,
	Return,
};

constexpr bool IsSpaceEquiv(int state) noexcept {
	return state <= SCE_GO_TASKMARKER;
}

constexpr int GetStringQuote(int state) noexcept {
	return (state == SCE_GO_CHARACTER) ? '\'' : ((state == SCE_GO_RAW_STRING) ? '`' : '\"');
}

// https://pkg.go.dev/fmt

constexpr bool IsFormatSpecifier(char ch) noexcept {
	return AnyOf(ch, 'v',
					'b',
					'c',
					'd',
					'e', 'E',
					'f', 'F',
					'g', 'G',
					'o', 'O',
					'p',
					'q',
					's',
					't', 'T',
					'U',
					'x', 'X');
}

Sci_Position CheckFormatSpecifier(const StyleContext &sc, LexAccessor &styler, bool insideUrl) noexcept {
	if (sc.chNext == '%') {
		return 2;
	}
	if (insideUrl && IsHexDigit(sc.chNext)) {
		// percent encoded URL string
		return 0;
	}
	if (IsASpaceOrTab(sc.chNext) && IsADigit(sc.chPrev)) {
		// ignore word after percent: "5% x"
		return 0;
	}

	Sci_PositionU pos = sc.currentPos + 1;
	// flags
	char ch = styler[pos];
	while (AnyOf(ch, ' ', '+', '-', '#', '0')) {
		ch = styler[++pos];
	}
	// argument index
	if (ch == '[') {
		ch = styler[++pos];
		while (IsADigit(ch)) {
			ch = styler[++pos];
		}
		if (ch == ']') {
			ch = styler[++pos];
		} else {
			return 0;
		}
	}
	// width
	if (ch == '*') {
		ch = styler[++pos];
	} else if (ch == '[') {
		ch = styler[++pos];
		while (IsADigit(ch)) {
			ch = styler[++pos];
		}
		if (ch == ']') {
			ch = styler[++pos];
		} else {
			return 0;
		}
	} else {
		while (IsADigit(ch)) {
			ch = styler[++pos];
		}
	}
	// precision
	if (ch == '.') {
		ch = styler[++pos];
		if (ch == '*') {
			ch = styler[++pos];
		} else if (ch == '[') {
			ch = styler[++pos];
			while (IsADigit(ch)) {
				ch = styler[++pos];
			}
			if (ch == ']') {
				ch = styler[++pos];
			} else {
				return 0;
			}
		} else {
			while (IsADigit(ch)) {
				ch = styler[++pos];
			}
		}
	}
	// verb
	if (IsFormatSpecifier(ch)) {
		return pos - sc.currentPos + 1;
	}
	return 0;
}

inline int DetectIdentifierType(LexAccessor &styler, GoFunction funcState, int chNext, Sci_Position startPos, Sci_Position lineStartCurrent) noexcept {
	if (((funcState == GoFunction::Caller || funcState == GoFunction::Return) && (chNext == ')' || chNext == ','))
		|| (funcState > GoFunction::Name && chNext == '{')) {
		// func (identifier *Type) (Type, error)
		// func (identifier Type) Type
		return SCE_GO_TYPE;
	}

	Sci_Position pos = --startPos;
	uint8_t ch = 0;
	while (pos > lineStartCurrent) {
		ch = styler[pos];
		if (!IsASpaceOrTab(ch)) {
			break;
		}
		--pos;
	}

	const bool star = (ch == '*' && pos == startPos);
	uint8_t chPrev = styler.SafeGetCharAt(pos - 1);
	const bool space = IsASpaceOrTab(chPrev);

	if (star) {
		if (chNext == ':' && space) {
			// case *Type:
			return SCE_GO_TYPE;
		}

		--pos;
		while (pos > lineStartCurrent) {
			ch = styler[pos];
			if (!IsASpaceOrTab(ch)) {
				break;
			}
			--pos;
		}

		chPrev = styler.SafeGetCharAt(pos - 1);
		if (ch == '-' && chPrev == '<') {
			// chan<- *Type
			return SCE_GO_TYPE;
		}
	} else if (ch == '&') {
		if (chNext == '{' && chPrev != '&') {
			// &Type{}
			return SCE_GO_TYPE;
		}
		return SCE_GO_DEFAULT;
	}

	if ((ch == '(' && chPrev == '.')
		|| ch == ']'
		|| (chNext == '{' && (ch == ':' || (ch == '=' && (chPrev == ':' || !isoperator(chPrev)))))
	) {
		// .(*Type), .(Type)
		// []*Type, []Type, [...]Type, [ArrayLength]Type, map[KeyType]ElementType
		// identifier = Type{}, identifier: Type{}, identifier := Type{}
		return SCE_GO_TYPE;
	}
	if ((!star || space) && IsIdentifierCharEx(ch)) {
		// identifier *Type, identifier Type
		return SCE_GO_TYPE;
	}
	return SCE_GO_DEFAULT;
}

void ColouriseGoDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList keywordLists, Accessor &styler) {
	int lineStateLineComment = 0;
	GoFunction funcState = GoFunction::None;
	KeywordType kwType = KeywordType::None;

	int visibleChars = 0;
	int visibleCharsBefore = 0;
	int chBefore = 0;
	int chPrevNonWhite = 0;
	bool insideUrl = false;
	EscapeSequence escSeq;

	StyleContext sc(startPos, lengthDoc, initStyle, styler);
	if (startPos != 0 && IsSpaceEquiv(initStyle)) {
		LookbackNonWhite(styler, startPos, SCE_GO_TASKMARKER, chPrevNonWhite, initStyle);
	}

	Sci_Position identifierStartPos = 0;
	Sci_Position lineStartCurrent = styler.LineStart(sc.currentLine);

	while (sc.More()) {
		switch (sc.state) {
		case SCE_GO_OPERATOR:
			sc.SetState(SCE_GO_DEFAULT);
			break;

		case SCE_GO_NUMBER:
			if (!IsDecimalNumberEx(sc.chPrev, sc.ch, sc.chNext)) {
				sc.SetState(SCE_GO_DEFAULT);
			}
			break;

		case SCE_GO_IDENTIFIER:
			if (!IsIdentifierCharEx(sc.ch)) {
				char s[128];
				sc.GetCurrent(s, sizeof(s));
				const KeywordType kwPrev = kwType;
				if (keywordLists[KeywordIndex_Keyword].InList(s)) {
					sc.ChangeState(SCE_GO_WORD);
					if (StrEqual(s, "func")) {
						funcState = (visibleChars == 4)? GoFunction::Define : GoFunction::Param;
					} else if (StrEqual(s, "type")) {
						kwType = KeywordType::Type;
					} else if (StrEqual(s, "const")) {
						kwType = KeywordType::Constant;
					} else if (StrEqualsAny(s, "map", "chan")) {
						kwType = KeywordType::Identifier;
					} else if (StrEqualsAny(s, "goto", "break", "continue")) {
						kwType = KeywordType::Label;
					}
					if (kwType == KeywordType::Type || kwType == KeywordType::Label) {
						const int chNext = sc.GetLineNextChar();
						if (!IsIdentifierStartEx(chNext)) {
							kwType = KeywordType::None;
						}
					}
				} else if (keywordLists[KeywordIndex_PrimitiveType].InList(s)) {
					sc.ChangeState(SCE_GO_WORD2);
				} else if (keywordLists[KeywordIndex_BuiltinFunction].InListPrefixed(s, '(')) {
					sc.ChangeState(SCE_GO_BUILTIN_FUNC);
					if (sc.ch == '(' && StrEqual(s, "new")) {
						kwType = KeywordType::Identifier;
					}
				} else if (keywordLists[KeywordIndex_Type].InList(s)) {
					sc.ChangeState(SCE_GO_TYPE);
				} else if (keywordLists[KeywordIndex_Struct].InList(s)) {
					sc.ChangeState(SCE_GO_STRUCT);
				} else if (keywordLists[KeywordIndex_Interface].InList(s)) {
					sc.ChangeState(SCE_GO_INTERFACE);
				} else if (keywordLists[KeywordIndex_Constant].InList(s)) {
					sc.ChangeState(SCE_GO_CONSTANT);
				} else if (sc.ch == ':') {
					if (sc.chNext != '=') {
						if (chBefore == ',' || chBefore == '{') {
							sc.ChangeState(SCE_GO_KEY);
						} else if (IsJumpLabelPrevASI(chBefore)) {
							sc.ChangeState(SCE_GO_LABEL);
						}
					}
				} else {
					const int chNext = sc.GetLineNextChar();
					if (chNext == '(') {
						if (funcState != GoFunction::None) {
							funcState = GoFunction::Name;
							sc.ChangeState(SCE_GO_FUNCTION_DEFINITION);
						} else {
							sc.ChangeState(SCE_GO_FUNCTION);
						}
					} else if (sc.Match('{', '}')) {
						// Type{}
						sc.ChangeState(SCE_GO_TYPE);
					} else if (kwType != KeywordType::None) {
						if (kwType == KeywordType::Type) {
							const Sci_Position pos = LexSkipWhiteSpace(styler, sc.currentPos + 1, sc.lineStartNext);
							if (chNext == 'i' && styler.Match(pos, "interface")) {
								kwType = KeywordType::Interface;
							} else if (chNext == 's' && styler.Match(pos, "struct")) {
								kwType = KeywordType::Struct;
							}
						} else if (kwType == KeywordType::Identifier && chNext != '.') {
							// map[KeyType]ElementType
							// chan ElementType
							// new(Type)
							kwType = KeywordType::Type;
						}
						if (kwType != KeywordType::Identifier) {
							sc.ChangeState(static_cast<int>(kwType));
							kwType = KeywordType::None;
						}
					} else if (!(chNext == '.' || chNext == '*')) {
						const int state = DetectIdentifierType(styler, funcState, chNext, identifierStartPos, lineStartCurrent);
						if (state != SCE_GO_DEFAULT) {
							sc.ChangeState(state);
						}
					}
				}

				if (sc.state == SCE_GO_WORD || sc.state == SCE_GO_WORD2) {
					identifierStartPos = lineStartCurrent = sc.currentPos;
				}
				if (kwType != KeywordType::None && kwPrev == kwType && sc.ch != '.') {
					kwType = KeywordType::None;
				}
				sc.SetState(SCE_GO_DEFAULT);
			}
			break;

		case SCE_GO_COMMENTLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_GO_DEFAULT);
			} else if (visibleChars - visibleCharsBefore == 2
				&& ((sc.ch == '+' && sc.Match("+build")) || sc.Match('g', 'o', ':'))) {
				sc.SetState(SCE_GO_TASKMARKERLINE);
			} else {
				HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_GO_TASKMARKER);
			}
			break;

		case SCE_GO_TASKMARKERLINE:
			if (sc.atLineStart) {
				sc.SetState(SCE_GO_DEFAULT);
			}
			break;

		case SCE_GO_COMMENTBLOCK:
			if (sc.Match('*', '/')) {
				sc.Forward();
				sc.ForwardSetState(SCE_GO_DEFAULT);
			} else if (HighlightTaskMarker(sc, visibleChars, visibleCharsBefore, SCE_GO_TASKMARKER)) {
				continue;
			}
			break;

		case SCE_GO_CHARACTER:
		case SCE_GO_STRING:
		case SCE_GO_RAW_STRING:
			if (sc.atLineStart && sc.state != SCE_GO_RAW_STRING) {
				sc.SetState(SCE_GO_DEFAULT);
			} else if (sc.ch == '\\' && sc.state != SCE_GO_RAW_STRING) {
				if (escSeq.resetEscapeState(sc.state, sc.chNext)) {
					sc.SetState(SCE_GO_ESCAPECHAR);
					sc.Forward();
				}
			} else if (sc.ch == GetStringQuote(sc.state)) {
				sc.Forward();
				if (sc.state == SCE_GO_STRING && (chBefore == ',' || chBefore == '{')) {
					const int chNext = sc.GetLineNextChar();
					if (chNext == ':') {
						sc.ChangeState(SCE_GO_KEY);
					}
				}
				sc.SetState(SCE_GO_DEFAULT);
			} else if (sc.state != SCE_GO_CHARACTER) {
				if (sc.ch == '%' && sc.state != SCE_GO_CHARACTER) {
					const Sci_Position length = CheckFormatSpecifier(sc, styler, insideUrl);
					if (length != 0) {
						const int state = sc.state;
						sc.SetState(SCE_GO_FORMAT_SPECIFIER);
						sc.Advance(length);
						sc.SetState(state);
						continue;
					}
				} else if (sc.Match(':', '/', '/') && IsLowerCase(sc.chPrev)) {
					insideUrl = true;
				} else if (insideUrl && IsInvalidUrlChar(sc.ch)) {
					insideUrl = false;
				}
			}
			break;

		case SCE_GO_ESCAPECHAR:
			if (escSeq.atEscapeEnd(sc.ch)) {
				sc.SetState(escSeq.outerState);
				continue;
			}
			break;
		}

		if (sc.state == SCE_GO_DEFAULT) {
			if (sc.Match('/', '/')) {
				visibleCharsBefore = visibleChars;
				sc.SetState(SCE_GO_COMMENTLINE);
				if (visibleChars == 0) {
					lineStateLineComment = SimpleLineStateMaskLineComment;
				}
			} else if (sc.Match('/', '*')) {
				visibleCharsBefore = visibleChars;
				sc.SetState(SCE_GO_COMMENTBLOCK);
				sc.Forward();
			} else if (sc.ch == '\"') {
				insideUrl = false;
				chBefore = chPrevNonWhite;
				sc.SetState(SCE_GO_STRING);
			} else if (sc.ch == '\'') {
				sc.SetState(SCE_GO_CHARACTER);
			} else if (sc.ch == '`') {
				insideUrl = false;
				sc.SetState(SCE_GO_RAW_STRING);
			} else if (IsNumberStart(sc.ch, sc.chNext)) {
				sc.SetState(SCE_GO_NUMBER);
			} else if (IsIdentifierStartEx(sc.ch)) {
				chBefore = chPrevNonWhite;
				if (sc.chPrev != '.') {
					identifierStartPos = sc.currentPos;
				}
				sc.SetState(SCE_GO_IDENTIFIER);
			} else if (IsAGraphic(sc.ch)) {
				sc.SetState(SCE_GO_OPERATOR);
				if (funcState != GoFunction::None) {
					switch (sc.ch) {
					case '(':
						switch (funcState) {
						case GoFunction::Define:
							funcState = GoFunction::Caller;
							break;
						case GoFunction::Caller:
						case GoFunction::Name:
							funcState = GoFunction::Param;
							break;
						case GoFunction::Param:
							funcState = GoFunction::Return;
							break;
						default:
							break;
						}
						break;
					case ')':
						if (funcState == GoFunction::Param) {
							funcState = GoFunction::Return;
						}
						break;
					case '{':
						if (!(sc.chPrev == 'e' && sc.chNext == '}')) {
							// interface{}
							funcState = GoFunction::None;
						}
						break;
					}
				} else if (sc.ch == ')' && IsASpaceOrTab(sc.chNext) && sc.GetLineNextChar(true) == '(') {
					funcState = GoFunction::Return;
				}
			}
		}

		if (!isspacechar(sc.ch)) {
			visibleChars++;
			if (!IsSpaceEquiv(sc.state)) {
				chPrevNonWhite = sc.ch;
			}
		}
		if (sc.atLineEnd) {
			styler.SetLineState(sc.currentLine, lineStateLineComment);
			lineStateLineComment = 0;
			visibleChars = 0;
			visibleCharsBefore = 0;
			funcState = GoFunction::None;
			lineStartCurrent = sc.lineStartNext;
			identifierStartPos = 0;
		}
		sc.Forward();
	}

	sc.Complete();
}

constexpr int GetLineCommentState(int lineState) noexcept {
	return lineState & SimpleLineStateMaskLineComment;
}

constexpr bool IsStreamCommentStyle(int style) noexcept {
	return style == SCE_GO_COMMENTBLOCK
		|| style == SCE_GO_TASKMARKER;
}

constexpr bool IsMultilineStringStyle(int style) noexcept {
	return style == SCE_GO_RAW_STRING
		|| style == SCE_GO_ESCAPECHAR
		|| style == SCE_GO_FORMAT_SPECIFIER;
}

void FoldGoDoc(Sci_PositionU startPos, Sci_Position lengthDoc, int initStyle, LexerWordList, Accessor &styler) {
	const Sci_PositionU endPos = startPos + lengthDoc;
	Sci_Line lineCurrent = styler.GetLine(startPos);
	int levelCurrent = SC_FOLDLEVELBASE;
	int lineCommentPrev = 0;
	if (lineCurrent > 0) {
		levelCurrent = styler.LevelAt(lineCurrent - 1) >> 16;
		lineCommentPrev = GetLineCommentState(styler.GetLineState(lineCurrent - 1));
		const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent - 1, SCE_GO_OPERATOR, SCE_GO_TASKMARKER);
		if (bracePos) {
			startPos = bracePos + 1; // skip the brace
		}
	}

	int levelNext = levelCurrent;
	int lineCommentCurrent = GetLineCommentState(styler.GetLineState(lineCurrent));
	Sci_PositionU lineStartNext = styler.LineStart(lineCurrent + 1);
	lineStartNext = sci::min(lineStartNext, endPos);

	int styleNext = styler.StyleAt(startPos);
	int style = initStyle;
	int visibleChars = 0;

	while (startPos < endPos) {
		const int stylePrev = style;
		style = styleNext;
		styleNext = styler.StyleAt(++startPos);

		switch (style) {
		case SCE_GO_COMMENTBLOCK:
			if (!IsStreamCommentStyle(stylePrev)) {
				levelNext++;
			} else if (!IsStreamCommentStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_GO_RAW_STRING:
			if (!IsMultilineStringStyle(stylePrev)) {
				levelNext++;
			} else if (!IsMultilineStringStyle(styleNext)) {
				levelNext--;
			}
			break;

		case SCE_GO_OPERATOR: {
			const char ch = styler[startPos - 1];
			if (ch == '{' || ch == '[' || ch == '(') {
				levelNext++;
			} else if (ch == '}' || ch == ']' || ch == ')') {
				levelNext--;
			}
		} break;
		}

		if (visibleChars == 0 && !IsSpaceEquiv(style)) {
			++visibleChars;
		}
		if (startPos == lineStartNext) {
			const int lineCommentNext = GetLineCommentState(styler.GetLineState(lineCurrent + 1));
			if (lineCommentCurrent) {
				levelNext += lineCommentNext - lineCommentPrev;
			} else if (visibleChars) {
				const Sci_PositionU bracePos = CheckBraceOnNextLine(styler, lineCurrent, SCE_GO_OPERATOR, SCE_GO_TASKMARKER);
				if (bracePos) {
					levelNext++;
					startPos = bracePos + 1; // skip the brace
					style = SCE_GO_OPERATOR;
					styleNext = styler.StyleAt(startPos);
				}
			}

			const int levelUse = levelCurrent;
			int lev = levelUse | levelNext << 16;
			if (levelUse < levelNext) {
				lev |= SC_FOLDLEVELHEADERFLAG;
			}
			if (lev != styler.LevelAt(lineCurrent)) {
				styler.SetLevel(lineCurrent, lev);
			}

			lineCurrent++;
			lineStartNext = styler.LineStart(lineCurrent + 1);
			lineStartNext = sci::min(lineStartNext, endPos);
			levelCurrent = levelNext;
			lineCommentPrev = lineCommentCurrent;
			lineCommentCurrent = lineCommentNext;
			visibleChars = 0;
		}
	}
}

}

LexerModule lmGo(SCLEX_GO, ColouriseGoDoc, "go", FoldGoDoc);
