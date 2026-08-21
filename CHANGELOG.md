# Leapcast Studio Changelog

> A complete history of meaningful improvements, fixes, and creator-focused features.

## Leapcast Studio 2.2.3

- Fixed pop-out chat messages becoming unreadable over white or bright backgrounds.
- Removed forced message backing boxes from the OBS overlay and pop-out.
- Added persistent background color, OBS background opacity, and 0–8 px chat-font outline controls under Settings.
- Made new pop-outs start at 0% opacity and remain correctly transparent through repeated resizing.
- Kept chat messages and the viewer count visible at 0% opacity; the slider now affects backgrounds only.
- Prevented Windows from exposing a white backing-store strip when a transparent pop-out is enlarged or resized back down.

---

## Leapcast Studio 2.2.1

**Transparent pop-outs, safe Program Files upgrades, and a complete Twitch Appeals review flow.**

| Release | Status | Focus |
|---|---|---|
| **V2.2.1** | Stable | Appeals, update safety, and Windows transparency |

### 🛡️ Twitch Appeals review

- Added **Approve Appeal** and **Deny Appeal** actions for one selected Twitch appeal.
- Added a polished, frameless review popup showing the account, ban reason, ban date, moderator, appeal response, and the user’s stored chat logs.
- The review popup closes safely when you click outside it. Clicking away never approves or denies an appeal.
- Successfully resolved appeals refresh automatically and disappear from the pending list.

### 🔐 Safer updates and installs

- New installations now default to `C:\Program Files\Leapcast Studio` on 64-bit Windows.
- Installer builds request the Windows administrator approval required for Program Files.
- Automatic updates stage installers in the Windows temporary directory—not Downloads—and reinstall over the current application path.
- Local settings, Twitch credentials, YouTube credentials, source links, moderation preferences, chat history, and event history stay outside the application folder so updates do not overwrite them.
- Added a once-per-version connection check that reports the local data path and confirms which existing Twitch and YouTube credentials were preserved.
- Added recovery from the pre-update settings backup if an older or interrupted installer leaves the settings file unreadable.

### 👁️ Pop-out transparency

- Fixed the native Windows backing surface that could remain black even when pop-out opacity was set to 0%.
- Made the pop-out host, chat viewport, and palette explicitly transparent while keeping message text readable.

### Permission note

V2.2.1 is the first public build requesting Twitch’s `moderator:manage:unban_requests` permission for the new appeal actions. Twitch and YouTube connection data remains preserved during updates.

---

## Leapcast Studio 2.2.0

**A smoother launch, dependable updates, persistent creator settings, and a workspace that adapts to you.**

| Release | Status | Focus |
|---|---|---|
| **V2.2.0** | Stable | Startup, customization, platform controls, and pop-out visibility |

### 🚀 Startup & updates

- Added the current version number to the launch screen in `V2.2.0` format.
- Added live “Checking for updates” status text to the launch screen.
- Moved the automatic release check before the main application window opens.
- New GitHub releases are downloaded and installed before opening the creator console.
- Improved update handoff so Leapcast Studio closes fully before files are replaced.
- Removed the slow full uninstall step from normal updates.
- Added an explicit post-update relaunch with fallback handling.

### 🔐 Data & authorization

- Preserved Twitch, YouTube, Streamlabs, source links, and user preferences across updates.
- Added a pre-update settings backup and automatic recovery if the primary settings file is damaged.
- Existing Twitch refresh tokens continue automatically when still valid.
- Reconnection is requested only when a platform expires, revokes, or invalidates authorization.
- Settings are saved immediately when changed and again before the application exits.

### 🎨 Settings & customization

- Added a sixth **Settings** navigation tab beneath OBS.
- Moved Automatic Updates from OBS into Settings.
- Added a persistent program font selector.
- Added persistent interface font-size controls.
- Added persistent button and control sizing.
- Added saved sidebar tab ordering with movement constrained to the sidebar.
- Added independent Twitch, YouTube, YouTube Shorts, and TikTok visibility switches.
- Disabled platforms disconnect and disappear from relevant source, chat, and moderation surfaces without deleting saved account data.

### 👁️ Pop-out chat

- Added a true 0% opacity setting.
- At 0%, the pop-out background and auxiliary controls become invisible while platform message text remains readable.
- The native window close button remains visible and usable.

### 🛡️ Moderation

- Improved right-click message identification in dashboard and pop-out chat.
- Restored Delete, Timeout, and Ban/Hide moderation actions when right-clicking supported platform messages.
- Preserved private `<Message Moderated>` notices outside the stream overlay.

---

### Upgrade note

Install **V2.2.0** normally. Leapcast Studio keeps compatible account authorization, source links, appearance preferences, and moderation data in AppData so the updated version can be used immediately after relaunch.
