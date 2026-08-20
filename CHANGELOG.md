# Leapcast Studio Changelog

> A complete history of meaningful improvements, fixes, and creator-focused features.

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
