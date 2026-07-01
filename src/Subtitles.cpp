#include "Subtitles.h"

#include <algorithm>
#include <cmath>

#include "Compatibility.h"
#include "ImGui/FontStyles.h"
#include "ImGui/Renderer.h"
#include "ImGui/Util.h"
#include "Manager.h"

Subtitle::Subtitle(const LocalizedSubtitle& a_subtitle) :
	fullLine(a_subtitle.subtitle),
	validForScaleform(RE::BSScaleformManager::GetSingleton()->IsValidName(a_subtitle.subtitle.c_str())),
	cached(a_subtitle)
{}

void Subtitle::WrapTextImpl()
{
	lines.clear();

	const auto& [text, maxChars, lang] = cached;
	// Wrap to the configured max-characters-per-line for both flat and VR; the MCM value is the
	// single source of truth (VR ships a narrower default via a per-platform settings overlay).
	std::uint32_t maxLineWidth = maxChars;

	if (IsTextCJK(text)) {
		WrapCJKText(lines, text, maxLineWidth);
	} else {
		WrapLatinText(lines, text, maxLineWidth);
	}

	// for drawing lines from bottom to top
	std::ranges::reverse(lines);
}

void Subtitle::WrapCJKText(std::vector<Line>& lines, const std::string& text, std::uint32_t maxLineWidth)
{
	Line   currentLine;
	size_t currentLineLength = 0;

	const auto flush_line = [&]() {
		float totalSize = 0.0f;
		for (const auto& word : currentLine.words) {
			totalSize += word.size.x;
		}
		currentLine.sizeX = totalSize;

		lines.push_back(currentLine);
		currentLine.words.clear();
		currentLineLength = 0;
	};

	std::size_t i = 0;

	while (i < text.size()) {
		auto charLen = GetUTF8CharLength(text, i);
		auto ch = text.substr(i, charLen);

		if (currentLineLength + ch.size() > maxLineWidth && !currentLine.words.empty()) {
			flush_line();
		}

		ImVec2 charSize = ImGui::CalcTextSize(ch.c_str());
		currentLine.words.emplace_back(ch, charSize, false);
		currentLineLength += ch.size();

		i += charLen;
	}

	if (!currentLine.words.empty()) {
		flush_line();
	}
}

void Subtitle::WrapLatinText(std::vector<Line>& lines, const std::string& text, std::uint32_t maxLineWidth)
{
	static const srell::regex fontRegex(R"(<font\s+face=['"]([^'"]+)['"]>(.*?)</font>)", srell::regex::optimize);

	std::vector<Word> currentWords;
	bool              currentDragonFont = false;
	std::size_t       currentWordsLength = 0;

	Line        currentLine;
	std::size_t currentLineLength = 0;

	const auto flush_words = [&]() {
		if (!currentWords.empty()) {
			std::string mergedText;
			for (std::size_t i = 0; i < currentWords.size(); ++i) {
				if (i > 0) {
					mergedText += " ";
				}
				mergedText += currentWords[i].word;
			}

			ImVec2 mergedSize;
			if (currentDragonFont) {
				ImGui::FontStyles::GetSingleton()->PushDragonFont();
				mergedSize = ImGui::CalcTextSize(mergedText.c_str());
				ImGui::PopFont();
			} else {
				mergedSize = ImGui::CalcTextSize(mergedText.c_str());
			}

			currentLine.words.emplace_back(mergedText, mergedSize, currentDragonFont);
			currentLineLength += mergedText.size();

			currentWords.clear();
			currentWordsLength = 0;
		}
	};
	const auto flush_line = [&]() {
		flush_words();

		float totalSize = 0.0f;
		for (std::size_t i = 0; i < currentLine.words.size(); ++i) {
			if (i > 0) {
				totalSize += ImGui::CalcTextSize(" ").x;
			}
			totalSize += currentLine.words[i].size.x;
		}
		currentLine.sizeX = totalSize;

		lines.push_back(currentLine);
		currentLine.words.clear();
		currentLineLength = 0;
	};
	const auto add_word = [&](const std::string& word, bool isDragonFont) {
		ImVec2 wordSize;
		if (isDragonFont) {
			ImGui::FontStyles::GetSingleton()->PushDragonFont();
			wordSize = ImGui::CalcTextSize(word.c_str());
			ImGui::PopFont();
		} else {
			wordSize = ImGui::CalcTextSize(word.c_str());
		}

		if (!currentWords.empty() && currentDragonFont != isDragonFont) {
			flush_words();
			if (!currentLine.words.empty()) {
				currentLineLength += 1;
			}
		}

		std::size_t spaceLength = currentWords.empty() ? 0 : 1;
		std::size_t newWordsLength = currentWordsLength + spaceLength + word.size();
		std::size_t totalLineLength = currentLineLength;

		if (!currentLine.words.empty() && currentWords.empty()) {
			totalLineLength += 1;
		}
		totalLineLength += newWordsLength;

		if (totalLineLength <= maxLineWidth || (currentLine.words.empty() && currentWords.empty())) {
			currentWords.emplace_back(word, wordSize, isDragonFont);
			currentWordsLength = newWordsLength;
			if (currentWords.size() == 1) {
				currentDragonFont = isDragonFont;
			}
		} else {
			if (!currentWords.empty()) {
				flush_words();
			}

			std::size_t wordLineLength = currentLineLength;
			if (!currentLine.words.empty()) {
				wordLineLength += 1;
			}
			wordLineLength += word.size();

			if (wordLineLength <= maxLineWidth || currentLine.words.empty()) {
				currentWords.emplace_back(word, wordSize, isDragonFont);
				currentWordsLength = word.size();
				currentDragonFont = isDragonFont;
			} else {
				flush_line();
				currentWords.emplace_back(word, wordSize, isDragonFont);
				currentWordsLength = word.size();
				currentDragonFont = isDragonFont;
			}
		}
	};

	for (const auto& token : SplitText(text)) {
		if (token == "<BR_MARKER>") {
			flush_line();
			continue;
		}

		bool        hasFontTag = false;
		bool        wordHasDragonFont = false;
		std::string strippedWord;

		srell::smatch match;
		std::string   remaining = token;

		while (srell::regex_search(remaining, match, fontRegex)) {
			hasFontTag = true;
			const std::string& font = match[1].str();
			const std::string& innerText = match[2].str();

			if (font == "$DragonFont") {
				wordHasDragonFont = true;
			}

			if (auto prefix = match.prefix().str(); !prefix.empty()) {
				strippedWord += prefix;
			}

			strippedWord += innerText;
			remaining = match.suffix().str();
		}
		strippedWord += remaining;

		if (hasFontTag && !strippedWord.empty()) {
			static const srell::regex trailingPunct(R"(^(.+?)([!?,.:;]+)$)");
			srell::smatch             punctMatch;

			if (srell::regex_match(strippedWord, punctMatch, trailingPunct)) {
				std::string mainWord = punctMatch[1].str();
				std::string punct = punctMatch[2].str();

				add_word(mainWord, wordHasDragonFont);
				add_word(punct, false);
			} else {
				add_word(strippedWord, wordHasDragonFont);
			}
		} else {
			add_word(strippedWord, wordHasDragonFont);
		}
	}

	if (!currentWords.empty() || !currentLine.words.empty()) {
		flush_line();
	}
}

std::vector<std::string> Subtitle::SplitText(const std::string& a_text)
{
	static const srell::regex br_tag(R"(<br\s*/?>)", srell::regex::optimize);
	static const srell::regex re(R"([^ \t\n\v\f\r<]*<[^>]+/>(?:[^ \t\n\v\f\r<>])*|[^ \t\n\v\f\r<]*<[^>]+>(?:[^<]|<(?!/))*?</[^>]+>(?:[^ \t\n\v\f\r<>])*|[^ \t\n\v\f\r]+)", srell::regex::optimize);

	std::vector<std::string> result;
	std::string              remaining = a_text;
	srell::smatch            match;

	while (srell::regex_search(remaining, match, br_tag)) {
		std::string prefix = match.prefix().str();
		if (!prefix.empty()) {
			for (auto it = srell::sregex_iterator(prefix.begin(), prefix.end(), re);
				 it != srell::sregex_iterator(); ++it) {
				result.push_back(it->str());
			}
		}

		result.push_back("<BR_MARKER>");

		remaining = match.suffix().str();
	}

	if (!remaining.empty()) {
		for (auto it = srell::sregex_iterator(remaining.begin(), remaining.end(), re);
			 it != srell::sregex_iterator(); ++it) {
			result.push_back(it->str());
		}
	}

	return result;
}

std::uint8_t Subtitle::GetUTF8CharLength(const std::string& str, std::size_t pos)
{
	const auto ch = static_cast<unsigned char>(str[pos]);
	if ((ch & 0x80) == 0) {  // ASCII
		return 1;
	}
	if ((ch & 0xE0) == 0xC0) {  // 2-byte UTF8
		return 2;
	}
	if ((ch & 0xF0) == 0xE0) {  // 3-byte UTF8
		return 3;
	}
	if ((ch & 0xF8) == 0xF0) {  // 4-byte UTF8
		return 4;
	}
	return 1;
}

bool Subtitle::IsTextCJK(const std::string& str)
{
	constexpr auto IsCJKCodePoint = [](char32_t cp) {
		return (cp >= 0x4E00 && cp <= 0x9FFF) ||
		       (cp >= 0x3400 && cp <= 0x4DBF) ||
		       (cp >= 0x20000 && cp <= 0x2EBEF) ||
		       (cp >= 0xF900 && cp <= 0xFAFF) ||
		       (cp >= 0x2F800 && cp <= 0x2FA1F) ||
		       (cp >= 0x3040 && cp <= 0x309F) ||
		       (cp >= 0x30A0 && cp <= 0x30FF) ||
		       (cp >= 0xAC00 && cp <= 0xD7AF);
	};

	std::size_t i = 0;
	while (i < str.size()) {
		auto charLen = GetUTF8CharLength(str, i);
		if (i + charLen > str.size()) {
			break;
		}

		char32_t cp = 0;
		switch (charLen) {
		case 1:
			cp = static_cast<unsigned char>(str[i]);
			break;
		case 2:
			cp = ((static_cast<unsigned char>(str[i]) & 0x1F) << 6) |
			     (static_cast<unsigned char>(str[i + 1]) & 0x3F);
			break;
		case 3:
			cp = ((static_cast<unsigned char>(str[i]) & 0x0F) << 12) |
			     ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 6) |
			     (static_cast<unsigned char>(str[i + 2]) & 0x3F);
			break;
		case 4:
			cp = ((static_cast<unsigned char>(str[i]) & 0x07) << 18) |
			     ((static_cast<unsigned char>(str[i + 1]) & 0x3F) << 12) |
			     ((static_cast<unsigned char>(str[i + 2]) & 0x3F) << 6) |
			     (static_cast<unsigned char>(str[i + 3]) & 0x3F);
			break;
		default:
			break;
		}

		if (IsCJKCodePoint(cp)) {
			return true;
		}

		i += charLen;
	}

	return false;
}

void Subtitle::WrapText()
{
	if (!isWrapped) {
		WrapTextImpl();
		isWrapped = true;
		if (!lines.empty()) {
			logger::debug("Subtitle wrapped into {} lines", lines.size());
		}
	}
}

void Subtitle::Invalidate()
{
	lines.clear();
	isWrapped = false;
}

void Subtitle::DrawSubtitle(float a_posX, float& a_posY, float a_alpha, float a_lineHeight, float a_fontScale, float a_elapsedTime, float a_duration) const
{
	if (a_alpha < 0.01f) {
		return;
	}

	if (lines.empty()) {
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a_alpha);

	// Cached word/line widths were measured unscaled at wrap time; scale them to match
	// the SetWindowFontScale applied for rendering so spacing/centering stays correct.
	const auto draw_single_line = [a_posX, a_fontScale](const Line& line, float yPos, float lineAlpha) {
		if (lineAlpha < 0.01f) {
			return;
		}
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, lineAlpha);
		float currentX = a_posX - (line.sizeX * 0.5f * a_fontScale);
		for (const auto& word : line.words) {
			if (word.isDragonFont) {
				ImGui::FontStyles::GetSingleton()->PushDragonFont();
			}

			const ImVec2 textPos(currentX, yPos);
			ImGui::SetCursorScreenPos(textPos);
			ImGui::Text(word.word.c_str());

			if (word.isDragonFont) {
				ImGui::PopFont();
			}
			currentX += word.size.x * a_fontScale;
		}
		ImGui::PopStyleVar();
	};

	if (Manager::GetSingleton()->GetSettings().scrollSubtitles && a_duration > 0.0f && lines.size() > 1) {
		const auto  n = lines.size();
		const float D = a_duration / static_cast<float>(n);
		int         j = static_cast<int>(std::floor(a_elapsedTime / D));
		j = std::clamp(j, 0, static_cast<int>(n) - 1);

		const float tau = a_elapsedTime - (static_cast<float>(j) * D);
		const float T_trans = std::min(0.5f, D * 0.5f);

		a_posY -= a_lineHeight;

		if (tau > D - T_trans && j < static_cast<int>(n) - 1) {
			float f = (tau - (D - T_trans)) / T_trans;
			f = std::clamp(f, 0.0f, 1.0f);

			draw_single_line(lines[n - 1 - j], a_posY - (f * a_lineHeight), a_alpha * (1.0f - f));
			draw_single_line(lines[n - 1 - (j + 1)], a_posY + ((1.0f - f) * a_lineHeight), a_alpha * f);
		} else {
			draw_single_line(lines[n - 1 - j], a_posY, a_alpha);
		}
	} else {
		for (const auto& line : lines) {
			a_posY -= a_lineHeight;
			draw_single_line(line, a_posY, a_alpha);
		}
	}

	ImGui::PopStyleVar();
}

DualSubtitle::DualSubtitle(const LocalizedSubtitle& a_primarySubtitle) :
	primary(a_primarySubtitle)
{}

DualSubtitle::DualSubtitle(const LocalizedSubtitle& a_primarySubtitle, const LocalizedSubtitle& a_secondarySubtitle) :
	primary(a_primarySubtitle),
	secondary(a_secondarySubtitle)
{}

void DualSubtitle::EnsureWrapped()
{
	primary.WrapText();
	secondary.WrapText();
}

void DualSubtitle::Invalidate()
{
	primary.Invalidate();
	secondary.Invalidate();
}

ImVec2 DualSubtitle::MeasureBlock(const ScreenParams& a_screenParams) const
{
	ImGui::SetWindowFontScale(a_screenParams.fontScale);
	const auto lineHeight = ImGui::GetTextLineHeight();

	float maxWidth = 0.0f;
	for (const auto& line : primary.lines) {
		maxWidth = std::max(maxWidth, line.sizeX);
	}
	for (const auto& line : secondary.lines) {
		maxWidth = std::max(maxWidth, line.sizeX);
	}
	maxWidth *= a_screenParams.fontScale;

	// Include the speaker-name line so the measured block isn't narrower than what DrawDualSubtitle
	// renders (CalcTextSize already reflects the active fontScale set above).
	if (!a_screenParams.speakerName.empty() && a_screenParams.alphaPrimary >= 0.01f) {
		const std::string nameLine = std::format("{}:", a_screenParams.speakerName);
		maxWidth = std::max(maxWidth, ImGui::CalcTextSize(nameLine.c_str()).x);
	}

	const bool isScrolling = Manager::GetSingleton()->GetSettings().scrollSubtitles && a_screenParams.duration > 0.0f;
	float      primaryLines = (primary.lines.size() > 1 && isScrolling) ? 1.0f : static_cast<float>(primary.lines.size());
	float      secondaryLines = (secondary.lines.size() > 1 && isScrolling) ? 1.0f : static_cast<float>(secondary.lines.size());
	float      totalLines = primaryLines;
	if (!secondary.lines.empty()) {
		totalLines += secondaryLines + a_screenParams.spacing;
	}
	if (!a_screenParams.speakerName.empty() && a_screenParams.alphaPrimary >= 0.01f) {
		totalLines += 1.0f;
	}

	ImGui::SetWindowFontScale(1.0f);
	return { maxWidth, totalLines * lineHeight };
}

ImVec2 DualSubtitle::DrawDualSubtitle(const ScreenParams& a_screenParams) const
{
	ImGui::SetWindowFontScale(a_screenParams.fontScale);

	// Scale the drop shadow with the apparent (distance) size: a far-off, shrunken subtitle
	// otherwise keeps the full-size offset, which reads as a separate offset copy. Clamp ≤ 1 so
	// it's never enlarged past the configured base — preserving the "thin outline" look up close.
	auto&        imStyle = ImGui::GetStyle();
	const ImVec2 baseShadowOffset = imStyle.TextShadowOffset;
	const float  shadowScale = std::min(a_screenParams.fontScale, 1.0f);
	imStyle.TextShadowOffset = { baseShadowOffset.x * shadowScale, baseShadowOffset.y * shadowScale };

	const auto lineHeight = ImGui::GetTextLineHeight();
	auto [posX, posY] = a_screenParams.pos;

	// Calculate subtitle dimensions to clamp within viewport boundaries
	float maxWidth = 0.0f;
	for (const auto& line : primary.lines) {
		if (line.sizeX > maxWidth) {
			maxWidth = line.sizeX;
		}
	}
	for (const auto& line : secondary.lines) {
		if (line.sizeX > maxWidth) {
			maxWidth = line.sizeX;
		}
	}
	maxWidth *= a_screenParams.fontScale;  // cached widths are unscaled; match the render scale

	// Include the speaker-name line so the clamp below isn't narrower than what gets rendered.
	if (!a_screenParams.speakerName.empty() && a_screenParams.alphaPrimary >= 0.01f) {
		const std::string nameLine = std::format("{}:", a_screenParams.speakerName);
		maxWidth = std::max(maxWidth, ImGui::CalcTextSize(nameLine.c_str()).x);
	}

	bool  isScrolling = Manager::GetSingleton()->GetSettings().scrollSubtitles && a_screenParams.duration > 0.0f;
	float primaryLines = (primary.lines.size() > 1 && isScrolling) ? 1.0f : static_cast<float>(primary.lines.size());
	float secondaryLines = (secondary.lines.size() > 1 && isScrolling) ? 1.0f : static_cast<float>(secondary.lines.size());

	float totalLines = primaryLines;
	if (!secondary.lines.empty()) {
		totalLines += secondaryLines + a_screenParams.spacing;
	}
	if (!a_screenParams.speakerName.empty() && a_screenParams.alphaPrimary >= 0.01f) {
		totalLines += 1.0f;
	}
	const float totalHeight = totalLines * lineHeight;

	const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	const float  paddingX = 20.0f;
	const float  paddingY = 20.0f;

	// World-quad subtitles are anchored to the speaker in world space and allowed to scroll off
	// the panel edge (cut off) for a world-locked feel, so skip the keep-on-screen clamp only in
	// that mode. Flat — and the VR non-world-quad fallback — still clamp so off-edge subtitles
	// stay readable at the viewport border.
	if (!ImGui::Renderer::WorldQuadActive()) {
		const float minX = (maxWidth * 0.5f) + paddingX;
		const float maxX = displaySize.x - (maxWidth * 0.5f) - paddingX;
		if (minX < maxX) {
			posX = std::clamp(posX, minX, maxX);
		} else {
			posX = displaySize.x * 0.5f;
		}

		const float minY = totalHeight + paddingY;
		const float maxY = displaySize.y - paddingY;
		if (minY < maxY) {
			posY = std::clamp(posY, minY, maxY);
		} else {
			posY = displaySize.y - paddingY;
		}
	}

	if (!secondary.lines.empty()) {
		posY -= lineHeight * a_screenParams.spacing;
		secondary.DrawSubtitle(posX, posY, a_screenParams.alphaSecondary, lineHeight, a_screenParams.fontScale, a_screenParams.elapsedTime, a_screenParams.duration);
	}

	primary.DrawSubtitle(posX, posY, a_screenParams.alphaPrimary, lineHeight, a_screenParams.fontScale, a_screenParams.elapsedTime, a_screenParams.duration);

	if (!a_screenParams.speakerName.empty() && a_screenParams.alphaPrimary >= 0.01f) {
		posY -= lineHeight;

		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a_screenParams.alphaPrimary);
		ImGui::PushStyleColor(ImGuiCol_Text, a_screenParams.speakerColor);

		const std::string line = std::format("{}:", a_screenParams.speakerName);
		const ImVec2      textPos(posX - (ImGui::CalcTextSize(line.c_str()).x * 0.5f), posY);

		ImGui::SetCursorScreenPos(textPos);
		ImGui::Text(line.c_str());

		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}

	imStyle.TextShadowOffset = baseShadowOffset;
	ImGui::SetWindowFontScale(1.0f);

	return { maxWidth, totalHeight };
}

std::string DualSubtitle::GetScaleformCompatibleSubtitle(bool a_dualSubs) const
{
	std::string subtitle;
	if (primary.validForScaleform) {
		subtitle = primary.fullLine;
	}
	if (a_dualSubs && secondary.validForScaleform) {
		if (!subtitle.empty()) {
			subtitle.append("\n");
		}
		subtitle.append(secondary.fullLine);
	}
	return subtitle;
}
