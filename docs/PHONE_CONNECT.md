# Phone Connect — iPhone Test

Phone Connect is an optional local control center for Leapcast Studio. Its
bottom navigation includes:

- **Chat:** Combined chat, viewer counts, platform filters, Delete, Timeout,
  and Ban controls.
- **Events:** Live follower, subscriber, member, donation, and supported alert
  events from the Windows session.
- **Moderation:** Twitch and YouTube restrictions, Unban controls, and Twitch
  unban-request Approve/Reject actions.
- **Settings:** AutoMod, overlay opacity, text outline, and message fade controls
  that update the connected Windows app immediately.

## Install

1. Start Leapcast Studio on the Windows PC.
2. Connect the Windows PC and iPhone to the same private Wi-Fi network.
3. Open **Phone Connect** in Leapcast Studio.
4. Select **Copy Install Link** and send the private link to the iPhone.
   Alternatively, point the iPhone Camera at the QR code and tap the Leapcast
   banner.
5. Open the link in Safari.
6. Tap **Share → Add to Home Screen**.
7. Keep **Open as Web App** enabled and tap **Add**.

## Requirements

- iPhone only for this test.
- iOS 16.4 or newer recommended; iOS 17 or newer preferred.
- iPhone 8 / iPhone X generation or newer; optimized for iPhone 16.
- Safari available for Home Screen installation.
- Windows 10 or Windows 11 PC running Leapcast Studio.
- Both devices on the same private Wi-Fi network.
- Private-network access allowed through Windows Firewall.
- Platform moderation connected inside Leapcast Studio before mobile actions
  are used.

The app is served locally by Leapcast Studio. It is not publicly hosted and
does not require an Apple Developer account, IPA signing, or the App Store.

Use **Reset / Generate New QR Code** before showcases or whenever a link may
have leaked. It rotates the private connection token.
This immediately invalidates the previous QR code, copied link, and any Home
Screen icon created from it.

## Windows release notifications

Phone Connect displays an update banner only after Leapcast confirms a newer
published GitHub Release containing a Windows installer. Ordinary commits,
branches, source pushes, and GitHub Actions runs do not trigger the banner.

Selecting **Update Windows App** starts the verified Windows updater. Leapcast
closes, installs the release, and relaunches. Return to Phone Connect afterward;
if the PC's local address changed, scan the QR code displayed in the relaunched
Windows app.

Leapcast also shows a short, scrollable update summary once on the first launch
of each installed version.
