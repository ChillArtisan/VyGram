# Local send templates

The library is available from the attachment menu in a regular chat and from
AyuGram Settings → General → Send templates. Settings opens management only;
start a send from the destination chat's attachment menu.

Each account (including the test-network distinction) has its own versioned
JSON library in AyuGram's encrypted private-settings database. The library
contains file paths, names, captions, types and IDs. It does not copy files or
upload them during creation, editing or preview. Moving a file requires choosing
its new path. Deleting a template leaves the original file on disk.

Supported inputs are deliberately narrower than ordinary attachments:

| Type | Accepted input |
| --- | --- |
| Round video | MP4, H.264 and AAC audio, square even dimensions up to 640×640, up to 60 seconds, up to 20 MiB |
| Voice | Ogg, mono Opus at 48 kHz with valid OpusHead metadata, up to 20 minutes and 20 MiB |
| Video | MP4, H.264, optional AAC audio, up to 128 MiB |
| Photo | Static JPEG/PNG, up to 20 MiB and 40 megapixels, width + height ≤ 10000, aspect ratio ≤ 20:1 |

The preview card shows the template's name, type, filename, caption and selected
delay. Start accepts 1–60 seconds. Local inspection runs off the main thread.
The first confirmed MTProto typing action starts the countdown; status updates
are renewed every five seconds. Photos use the upload-photo action with the
required progress field held at zero; the UI displays only the countdown and
does not invent upload percentages. Ghost-mode suppression and the existing
recipient/status eligibility rules are respected and explained before starting.

Cancellation stops the timer, cancels the outstanding status request and sends
`sendMessageCancelAction` for the original chat/topic. The same cleanup runs when
the preview closes, its chat context changes, the window closes, the account
changes, the file changes/disappears, rights change or connectivity fails.
Ordinary typing, recording or uploading in the same chat/topic interrupts the
template before the ordinary action proceeds. Only one template can run per
session. A failed or canceled operation never reaches the media queue.

At the deadline, the operation becomes terminal before handing its immutable
file snapshot to the existing media pipeline. Voice/round uses
`ApiWrap::sendVoiceMessage`; photo/video uses `ApiWrap::sendFiles`. Reply/topic,
send-as, paid-send approval and existing ghost scheduling remain in that
pipeline. Immediately before queuing a round video, the in-memory MP4 snapshot
gets the current UTC creation/modification time in movie, track and media
headers. Optional user metadata (including textual capture dates and XMP) is
replaced by equal-size empty atoms. Media packets, duration, track matrices,
chunk offsets and the original local file remain unchanged. Unsupported or
malformed atom layouts fail closed before uploading. This applies only to round
templates, not ordinary recording or attachments. Already sent files keep their
original metadata. After handoff, normal history/upload controls manage sending and its
errors. The template card does not own an upload or promise cancellation after
that handoff.

## Verification checklist

Do not build this checkout while `AGENTS.md` prohibits building. These are
runtime acceptance scenarios for a subsequent permitted build, **not recorded
as executed**. Use a second account/client to observe outgoing actions and media
types; Saved Messages, bots, offline/hidden-status users and broadcast channels
may be excluded by the existing status policy.

1. Create one template of each type. Restart and reopen the same account's
   library; verify name, type, file path and caption persist. Verify another
   account and the test network do not share the library.
2. Rename/edit a template, replace its file, delete it, and verify that deleting
   the template does not delete the local file. Verify locked/corrupt storage
   produces an error and does not overwrite existing data.
3. Open a template from an ordinary chat and a forum topic. Verify the card's
   contents and delay. Cancel before Start; verify no action and no upload.
4. Start each type with a delay longer than five seconds. Observe record-round,
   record-audio, record-video or upload-photo on the second client; verify it
   persists across refresh intervals. Verify no upload occurs before the
   countdown finishes. Check the resulting server media attributes: round flag,
   voice flag, video attribute, or photo media, respectively. Check the caption.
5. Double-click Start, use keyboard activation repeatedly and dismiss/retry a
   paid-send confirmation. Verify only one status run and one eventual media
   message, and that no paid-send confirmation is bypassed.
6. Cancel immediately, during an in-flight status request, immediately before
   the deadline and while a status refresh is pending. Verify no file is sent,
   the timer stops and the remote indicator clears.
7. During the countdown, switch chat/topic, close the window (including close to
   tray), switch/logout the account, remove/replace the file, disable upload
   status sharing, close the forum topic or revoke the relevant media right.
   Verify cleanup and no send. Test a late native file-picker response after
   closing the originating card/account.
8. Disconnect before Start and during the countdown. Test a failing/false
   setTyping response and one that never completes. Verify no send, no repeated
   retry loop and best-effort cancel; offline clients cannot deliver cancellation
   until the connection returns, and Telegram's own action expiry still applies.
9. Reject MP3/M4A renamed to Ogg, Vorbis-in-Ogg, non-square or overlong round
   video, unsupported codecs, animated photos, directories, zero-byte and
   oversized files. Verify the UI explains the supported input.
10. Exercise normal microphone recording, round recording, text typing and
    photo/video attachments, including activity from a second window in the
    same chat. Verify the template yields to ordinary activity and its terminal
    timer cannot send later. Check unrelated chat uploads do not stop it.
11. Check keyboard navigation, long names/captions and 100%, 150% and 200% UI
    scale with Terminal and the standard light theme. New widgets use existing
    scalable styles and localization keys.

12. Send a round template with old MP4 creation dates and textual capture tags,
    save it on the recipient client and inspect its metadata. Standard movie,
    track and media dates should match the send time; original capture tags
    should be absent. Compare decoded audio/video hashes and original local
    file bytes. Repeat after waiting on the preview and with the same template
    a second time to ensure the date refreshes at each queue handoff.
