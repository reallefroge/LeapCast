<div align="center">
  <img src="resources/lefroge_chat_icon.png" alt="Leapcast Studio frog logo" width="120">

  # 🐸 Leapcast Studio

  **One clean command center for multi-platform chat, moderation, alerts, and OBS.**

  [![Version](https://img.shields.io/badge/version-2.3.0-7c5cff?style=for-the-badge)](https://github.com/reallefroge/LeapCast/releases)
  [![Windows](https://img.shields.io/badge/Windows-10%20%7C%2011-31b8ff?style=for-the-badge&logo=windows11)](https://github.com/reallefroge/LeapCast/releases)
  [![License](https://img.shields.io/badge/license-MIT-58d68d?style=for-the-badge)](LICENSE)
  [![Free](https://img.shields.io/badge/price-free-ff4f81?style=for-the-badge)](#-free--local-first)

  **Twitch · YouTube · YouTube Shorts · TikTok LIVE · Kick · Rumble**

  [⬇️ Download](https://github.com/reallefroge/LeapCast/releases) · [✨ What’s new](CHANGELOG.md) · [🚀 Quick start](#-quick-start) · [🐛 Report a bug](https://github.com/reallefroge/LeapCast/issues)
</div>

> [!IMPORTANT]
> Leapcast Studio works **beside OBS**. It manages chat, moderation, alerts, bans, and a browser-source overlay—it does not encode or broadcast video.

## ✨ Built for busy live chats

| 💬 Unified chat | 🛡️ Creator safety | 🎉 Live alerts | 🎨 Stream-ready |
|---|---|---|---|
| Six platform feeds plus one combined view | Blocked words, spam checks, Twitch AutoMod, and moderation shortcuts | Streamlabs events plus Rumble follow and subscription alerts | Custom OBS overlay and always-on-top pop-out chat |

Everything stays inside one focused Windows app, so you can watch the conversation without juggling browser tabs.

## 📸 See it in action

### Connect every community

Add each channel independently and switch between the combined feed or branded platform tabs.

![Leapcast Studio Sources page](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-sources.png)

### Bring alerts into the conversation

Streamlabs and Rumble activity appears in the preview and pop-out without entering the OBS chat feed.

![Leapcast Studio Events page](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-events.png)

### Moderate from one safety center

Manage blocked words, Twitch AutoMod timing, TikTok LIVE controls, and supported moderation shortcuts.

![Leapcast Studio Creator Safety Center](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-moderation.png)

> [!NOTE]
> Rumble currently opens its own live-chat moderation page because Rumble does not provide the required moderation API. Kick moderation remains hidden until authenticated API support is available.

### Review bans and timeouts

Inspect supported restrictions, view details, refresh the list, and remove bans from a focused workspace.

![Leapcast Studio Bans and Timeouts page](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-bans.png)

### Shape the overlay around your stream

Copy the browser-source URL, send a test message, clear the feed, change opacity, adjust text outlines, and control message fading.

![Leapcast Studio Chat Overlay page](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-chat-overlay.png)

### Make it yours

Adjust app scaling, pop-out fonts, name colors, outline thickness, platform visibility, and sidebar order.

![Leapcast Studio Settings page](https://raw.githubusercontent.com/reallefroge/LeapCast/main/leapcast-settings.png)

## 🚀 Quick start

1. Download the newest `LeapcastStudio-Setup-<version>.exe` from [Releases](https://github.com/reallefroge/LeapCast/releases).
2. Install and open **Leapcast Studio**.
3. Add your channel or live-stream links under **Sources**.
4. Connect only the communities you want to monitor.
5. Open **Chat Overlay** → **Copy URL**.
6. Add the URL to OBS as a **Browser Source**.
7. Send a **Test message** before going live.

No Python, CMake, Visual Studio, Qt SDK, or separate runtime setup is required for the Windows installer.

> [!WARNING]
> Reload or reconnect Twitch and YouTube authorization under **Keys after every update**. Never share access tokens, API URLs, client secrets, stream keys, or your settings file.

## 🌐 Platform support

| Platform | Chat | Viewer data | In-app moderation | Notes |
|---|:---:|:---:|:---:|---|
| Twitch | ✅ | ✅ | ✅ | Authorization, AutoMod timeouts, bans, appeals, and clips |
| YouTube | ✅ | ✅ | ✅ | Live and Shorts chat with supported moderation actions |
| TikTok LIVE | ✅ | ✅ | ↗️ | Opens the creator’s LIVE controls in your browser |
| Kick | ✅ | — | — | Chat and local blocked-word filtering; moderation hidden until API support exists |
| Rumble | ✅ | ✅ | ↗️ | Opens Rumble moderation; private Live Stream API URL powers alerts |

## 🧰 Feature highlights

### 💬 Chat that stays readable

- Combined **All** feed plus separate platform tabs.
- Random or fixed username color schemes.
- Custom pop-out font, size, outline thickness, opacity, and click-through mode.
- Platform toggles enabled by default.
- Clear controls for both the pop-out and OBS overlay feeds.
- Blocked Kick and Rumble messages are rejected before display or audit storage.

### 🛡️ Moderation without the clutter

- Shared editable blocked-word list.
- Detection for leetspeak, separators, repeated letters, Unicode lookalikes, and close misspellings.
- Spam, flooding, excessive capitals, promotion, and optional link checks.
- Configurable Twitch AutoMod timeout with per-broadcast escalation.
- Twitch ban, timeout, and appeal tools.
- YouTube moderation history for actions created through Leapcast Studio.

### 🎉 Alerts that feel native

- Streamlabs donations, follows, subscriptions, memberships, Super Chats, Bits, raids, and hosts.
- Optional Streamlabs Alert Box audio and visuals inside the pop-out.
- Rumble followers, subscribers, and gifted subscriptions use the same Leapcast alert presentation.
- Existing Rumble activity is ignored during connection so only new events trigger alerts.

### 🎨 OBS and pop-out control

- Local overlay at `http://127.0.0.1:8080/` by default.
- Background color and opacity controls.
- Custom chat text outline thickness.
- Configurable message fade timer.
- Test and clear actions that update the live browser-source URL.
- Always-on-top pop-out with combined viewer totals.

## 🔑 Keys and authorization

- **Twitch:** authorize through the browser and reload access after updates.
- **YouTube:** configure YouTube Data API/OAuth access, then reload it after updates.
- **Kick:** no key is currently required for chat.
- **Rumble:** open [Rumble’s Live Stream API page](https://rumble.com/account/livestream-api), copy the entire private API URL, paste it under **Keys**, then choose **Save & Connect Rumble**.

Keep the Rumble API URL private. If it is exposed, reset it on Rumble and replace the saved URL.

## 🔄 Updates

Leapcast checks GitHub Releases for updates and shows a compact, readable patch-notes dialog. The updater downloads the installer, preserves settings stored in AppData, installs the new version, and reopens the app.

> [!NOTE]
> GitHub’s automatically generated **Source code** ZIP files are for developers. Most users should download the Windows installer from the release assets.

## 🔒 Free & local-first

- Completely free—no subscription, trial, paid tier, or feature paywall.
- Chat and event audit data stays on the local Windows computer.
- The OBS browser source is served locally.
- No Python runtime or Python subprocess.
- Creator tokens are not compiled into the public repository.

Authentication values are currently stored in the app’s local Windows application-data folder rather than Windows Credential Manager. Protect your Windows account and do not share your settings file.

## ⚠️ Current limitations

- Windows 10/11 x64 only.
- Leapcast does not broadcast or multistream video.
- Platform features may change when a service changes its API or web interface.
- YouTube’s Bans page is a Leapcast moderation history, not a complete server-side record of every action.
- New unsigned installers may trigger Microsoft Defender SmartScreen while download reputation develops.

## 🧑‍💻 Build from source

### Requirements

- Visual Studio 2022 with **Desktop development with C++**
- Qt 6.5+ for MSVC 2022 x64, including WebEngine and WebSockets
- CMake 3.24+
- Inno Setup 6 for the installer

```powershell
.\build-installer.ps1
```

The script reads `VERSION`, builds the app, bundles Qt, and writes the installer plus its SHA-256 file to `artifacts`. See [PUBLISHING.md](PUBLISHING.md) for the release workflow.

## 🤝 Support and contributions

- [Open an issue](https://github.com/reallefroge/LeapCast/issues) with the app version, platform, expected result, actual result, and a privacy-safe screenshot.
- Fork the repository and open a focused pull request for contributions.
- Never publish tokens, private API URLs, client secrets, stream keys, or unblurred chat logs.

<div align="center">

---

**Leapcast Studio v2.3.0** · Built by Lefroge with C++20 and Qt 6 · [MIT License](LICENSE)

</div>
