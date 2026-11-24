// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/ayu_lang.h"

#include "lang/lang_keys.h"
#include <QRegularExpression>

QByteArray AyuLanguage::brandedValue(const QByteArray &key, const QByteArray &value) {
	if (key == "ayu_FirstLaunchAlert" || key == "ayu_ExteraChatsAlert"
		|| key == "ayu_SettingsWatermark") {
		return Lang::GetOriginalValue(Lang::GetKeyIndex(QLatin1String(key))).toUtf8();
	}
	for (const auto brandKey : {
		"ayu_AyuPreferences", "ayu_KeepAliveService", "ayu_ResetSettingsConfirmation",
		"ayu_ExportDataTitle", "ayu_ImportDataTitle", "ayu_IconAlternative",
		"ayu_AyuAttachments", "ayu_AyuDatabase", "ayu_PluginsNotAvailable",
		"ayu_IntroAbout", "ayu_UpdateAyuGram" }) {
		if (key == brandKey) {
			static const auto tokens = QRegularExpression(
				uR"(((?:[a-z][a-z0-9+.-]*://|www\.|@)[^\s<>]+|[\w.+-]+@[\w.-]+|(?:[\w-]+\.)+[\p{L}]{2,}(?:/[^\s<>]*)?)|\bAyuGram\b)"_q,
				QRegularExpression::CaseInsensitiveOption);
			const auto text = QString::fromUtf8(value);
			auto result = QString();
			auto offset = qsizetype(0);
			auto matches = tokens.globalMatch(text);
			while (matches.hasNext()) {
				const auto match = matches.next();
				result += text.mid(offset, match.capturedStart() - offset);
				result += match.captured(1).isEmpty() ? u"VyGram"_q : match.captured(0);
				offset = match.capturedEnd();
			}
			result += text.mid(offset);
			return result.toUtf8();
		}
	}
	return value;
}
