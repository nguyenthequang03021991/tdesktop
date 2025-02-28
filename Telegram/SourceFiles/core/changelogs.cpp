/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/changelogs.h"

#include "lang/lang_keys.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "storage/storage_domain.h"
#include "data/data_session.h"
#include "mainwindow.h"
#include "apiwrap.h"

namespace Core {
namespace {

std::map<int, const char*> BetaLogs() {
	return {
	{
		2007005,
		"- Add \"Voice chats\" filter in \"Recent actions\" for channels.\n"

		"- Write local drafts to disk on a background thread.\n"

		"- Support autoupdate for Telegram in write-protected folders on Linux.\n"

		"- Fix crash in native notifications on Linux.\n"

		"- Fix crash in file dialog on Linux.\n"
	},
	{
		2007007,
		"- Optimized video playback in media viewer and Picture-in-Picture mode.\n"

		"- Added integration with System Media Transport Controls on Windows 10.\n"

		"- Added \"Now Playing\" integration for music playback on macOS.\n"

		"- Added \"Archive Sticker\" into the \"...\" menu of the Sticker Set Box.\n"

		"- Fixed memory not being freed on Linux.\n"

		"- Several crash fixes.\n"
	},
	{
		2007009,
		"- Added \"Enable noise suppression\" option to group calls Settings.\n"

		"- Fix media viewer with Retina + Non-Retina dual monitor setup on macOS.\n"

		"- Several bug and crash fixes.\n"
	},
	{
		2007010,
		"- Added ability to mix together bold, italic and other formatting.\n"

		"- Fix voice chats and video calls OpenGL with some drivers on Windows.\n"

		"- Several bug fixes.\n"
	},
	{
		2008006,
		"- Added a simple image editor. "
		"Crop photos or highlight parts of screenshots before sending.\n"

		"- Use Direct3D 9 backend in ANGLE by default (Windows).\n"

		"- Fix \"Show in Finder\" not focusing the Finder window (macOS).\n"

		"- Use GTK from a child process (Linux).\n"
	},
	{
		2008012,
		"- Change the default chat background.\n"

		"- Add GIFs overview section in chats.\n"

		"- Add a simple new messages animation.\n"

		"- Use modern Telegram application icon.\n"

		"- Use Direct3D 11 by default on Windows.\n"

		"- Fix Direct3D acceleration on basic Windows 7 setup.\n"
	},
	{
		2009004,
		"- Choose one from dozens of new gorgeous animated backgrounds"
		" in Chat Settings > Chat background.\n"
	},
	{
		2009005,
		"- Tile chat background patterns horizontally.\n"

		"- Fix a rare crash in spellchecker on Windows.\n"

		"- Fix animated chat backgrounds in Saved Messages.\n"

		"- Fix \"Sorry, group is inaccessible\" message "
		"in scheduled voice chats.\n",
	},
	{
		2009013,
		"- See unread comments count when scrolling discussions in channels."
	},
	};
};

} // namespace

Changelogs::Changelogs(not_null<Main::Session*> session, int oldVersion)
: _session(session)
, _oldVersion(oldVersion) {
	_session->data().chatsListChanges(
	) | rpl::filter([](Data::Folder *folder) {
		return !folder;
	}) | rpl::start_with_next([=] {
		requestCloudLogs();
	}, _chatsSubscription);
}

std::unique_ptr<Changelogs> Changelogs::Create(
		not_null<Main::Session*> session) {
	auto &local = Core::App().domain().local();
	const auto oldVersion = local.oldVersion();
	local.clearOldVersion();
	return (oldVersion > 0 && oldVersion < AppVersion)
		? std::make_unique<Changelogs>(session, oldVersion)
		: nullptr;
}

void Changelogs::requestCloudLogs() {
	_chatsSubscription.destroy();

	const auto callback = [this](const MTPUpdates &result) {
		_session->api().applyUpdates(result);

		auto resultEmpty = true;
		switch (result.type()) {
		case mtpc_updateShortMessage:
		case mtpc_updateShortChatMessage:
		case mtpc_updateShort:
			resultEmpty = false;
			break;
		case mtpc_updatesCombined:
			resultEmpty = result.c_updatesCombined().vupdates().v.isEmpty();
			break;
		case mtpc_updates:
			resultEmpty = result.c_updates().vupdates().v.isEmpty();
			break;
		case mtpc_updatesTooLong:
		case mtpc_updateShortSentMessage:
			LOG(("API Error: Bad updates type in app changelog."));
			break;
		}
		if (resultEmpty) {
			addLocalLogs();
		}
	};
	_session->api().requestChangelog(
		FormatVersionPrecise(_oldVersion),
		crl::guard(this, callback));
}

void Changelogs::addLocalLogs() {
	if (AppBetaVersion || cAlphaVersion()) {
		addBetaLogs();
	}
	if (!_addedSomeLocal) {
		const auto text = tr::lng_new_version_wrap(
			tr::now,
			lt_version,
			QString::fromLatin1(AppVersionStr),
			lt_changes,
			tr::lng_new_version_minor(tr::now),
			lt_link,
			Core::App().changelogLink());
		addLocalLog(text.trimmed());
	}
}

void Changelogs::addLocalLog(const QString &text) {
	auto textWithEntities = TextWithEntities{ text };
	TextUtilities::ParseEntities(textWithEntities, TextParseLinks);
	_session->data().serviceNotification(textWithEntities);
	_addedSomeLocal = true;
};

void Changelogs::addBetaLogs() {
	for (const auto [version, changes] : BetaLogs()) {
		addBetaLog(version, changes);
	}
}

void Changelogs::addBetaLog(int changeVersion, const char *changes) {
	if (_oldVersion >= changeVersion) {
		return;
	}
	const auto text = [&] {
		static const auto simple = u"\n- "_q;
		static const auto separator = QString::fromUtf8("\n\xE2\x80\xA2 ");
		auto result = QString::fromUtf8(changes).trimmed();
		if (result.startsWith(simple.midRef(1))) {
			result = separator.mid(1) + result.mid(simple.size() - 1);
		}
		return result.replace(simple, separator);
	}();
	const auto version = FormatVersionDisplay(changeVersion);
	const auto log = qsl("New in version %1 beta:\n\n").arg(version) + text;
	addLocalLog(log);
}

QString FormatVersionDisplay(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ ((version % 1000)
			? ('.' + QString::number(version % 1000))
			: QString());
}

QString FormatVersionPrecise(int version) {
	return QString::number(version / 1000000)
		+ '.' + QString::number((version % 1000000) / 1000)
		+ '.' + QString::number(version % 1000);
}

} // namespace Core
