# Leapcast Studio Changelog

> A complete history of meaningful improvements, fixes, and creator-focused features.

## Leapcast Studio 3.0.2

- Fixed v3 installations repeatedly offering an older v2 release as an update.
- Prevented the silent background check from reopening the Windows update dialog after startup.
- Kept Phone Connect release notifications available without duplicating the desktop prompt.

## Leapcast Studio 3.0.1

- 💬 Repaired TikTok LIVE chat capture with current selectors, direct added-node detection, compact viewer-count parsing, and automatic bridge rescans.
- 💜 Added Twitch Channel Point redemptions to Windows chat, pop-out chat, OBS, and Phone Connect chat/events. Existing Twitch connections must authorize the new redemption permission once.
- 🎉 Added optional TikTok join, follow, and like activity for the pop-out only. It is disabled by default and never enters OBS or Phone Connect.
- 🔖 Restored platform icons beside messages with separate settings for the program/pop-out and OBS overlay.
- 🔐 Preserved v3.0.0 QR reset, private Phone Connect controls, mobile release notifications, and first-launch update notes.

## Leapcast Studio 2.3.0

- Added Kick and Rumble sources, chat preview tabs, overlay routing, viewer counts, and settings toggles.
- Added native Rumble Live Stream API chat support with private API URL storage under Keys.
- Added Kick live web-chat capture and Rumble moderation launch actions; Kick moderation stays hidden until authenticated API support is implemented.
- Added branded Kick and Rumble chat tab artwork; both platforms are enabled by default.
- Added Rumble follower, subscriber, and gifted-subscription alerts through the same Leapcast alert pipeline as Streamlabs, without replaying old API history at startup.
- Kick and Rumble blocked-word messages are rejected before every visible chat surface and omitted from the stored chat log.
- Corrected the AutoMod control to Twitch, made its base timeout configurable, and retained per-broadcast escalation tracking.
- Condensed Settings into a non-scrolling layout and improved wrapping/spacing across pages to prevent clipped text.

- Fixed AutoMod's blocked-word list not taking effect after editing it. "Edit blocked words" opens the list in your text editor and reloaded it immediately afterward — before you'd actually changed and saved anything — so edits only ever took effect after restarting the app. The list now reloads automatically the moment the file is saved.
- AutoMod on Twitch now escalates repeat offenders within a broadcast instead of always issuing the same 300s timeout: a 300s timeout for offenses 1–5, a single 600s timeout for offense 6, then a permanent ban for anything after that. The count resets each time the channel goes live; AutoMod keeps moderating normally while the channel is offline in between.
- Added a "YouTube AutoMod timeout" setting (Moderation page) so moderators can choose how long AutoMod times a YouTube/Shorts chatter out for, from 300 to 86400 seconds (24 hours), instead of a fixed 300s.

## Leapcast Studio 2.2.8

- Removed the pop-out chat's text outline entirely — testing showed it rendering worse (chopped/unreadable) than plain text, unlike the OBS overlay's CSS outline, which is unaffected and unchanged. The "Chat font outline thickness" setting still applies to the OBS overlay only.
- Fixed the app not reopening after a silent auto-update. Inno Setup's own post-install relaunch depends on Windows correctly recovering the original (unelevated) user from our elevated installer process, which isn't always reliable; the app now also waits for the installer to finish and relaunches itself as a fallback if that didn't already happen.

## Leapcast Studio 2.2.7

- Fixed the pop-out window showing a distorted, stretched frame with duplicated text after being resized. The new frameless (v2.2.5) window has no automatic double-buffering safety net during a resize the way a normal composited window does, and clearing only the "damaged" rectangle on each paint could leave Windows compositing a stretched copy of the previous frame into the newly exposed area. Every paint now clears the full window instead, and a resize forces an immediate synchronous repaint.
- The pop-out's title bar (drag handle + ✕ close button) now has its own permanent, always-visible backdrop instead of fading with the rest of the window at low opacity — it was becoming impossible to spot.
- Fixed the chat font outline thickness not matching the pixel value set in Settings, and outlines getting clipped at the top/bottom of a line at larger thicknesses.

## Leapcast Studio 2.2.5

- Fixed the pop-out chat rendering as a solid white window instead of transparent. The pop-out kept its native Windows title bar, which relies on DWM glass composition for a translucent client area — when DWM composition or the system's "Transparency effects" setting isn't fully active, that composition silently fell back to an opaque white surface, and no amount of stylesheet/palette tuning could fix a compositor-level fallback. The pop-out is now a frameless, per-pixel-alpha window (the standard, compositor-independent approach), with its own small title bar (drag to move, ✕ to close) and a resize grip in the corner replacing the native ones.

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
