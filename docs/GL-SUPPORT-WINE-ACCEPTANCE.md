# Final candidate check in a separate macOS account

This checks independence from the developer's user account. It does not establish support for another physical Mac, GPU or macOS version. Do not copy an existing Wine bottle, Steam folder, game settings or download cache into the test account.

1. Exit TFA and Windows Steam in your current account. Create a **Standard** user in System Settings → Users & Groups → Add User, then log out and log in to that user. [Apple's account creation guide](https://support.apple.com/guide/mac-help/mchl3e281fc9/mac).
2. Download the **rc3** binary archive and SHA256SUMS from our unpublished draft release using a browser in that account. If draft-release access is inconvenient, transfer only those two release files through `/Users/Shared`; note that this skips browser-download/quarantine testing.
3. In Terminal, enter the download folder and verify the selected archive: `grep 'GL-support-wine-preview-1-rc3-macos-apple-silicon.tar.gz$' SHA256SUMS | shasum -a 256 -c -`. Extract it to a permanent folder in your new home directory.
4. Read its README. Do not install Homebrew, Python, Xcode or CrossOver for this test. If macOS blocks the unsigned/unnotarized software, record the exact message before changing anything; do not disable system security globally.
5. Open Terminal in the extracted `GL-support-wine-preview-1` folder. Run `./gl-support-wine setup > setup.log 2>&1`. Wait for it to finish; `echo $?` immediately afterwards should print `0`. Setup downloads prerequisites and Steam. You can watch progress from a second Terminal with `tail -f setup.log`.
6. Run `./gl-support-wine steam`, log in to your Steam account, and install TFA to the default C: library. Leave Steam open. In another Terminal in the package folder run `./gl-support-wine play-tfa > game.log 2>&1`.
7. Check menu music, loading transitions, museum rendering, and several minutes of a mission. Record any unusually long first 3D transition. Exit the game normally, launch it once more, then exit both TFA and Steam.

Report: setup exit status; any macOS security prompt; whether Steam login/download worked; music and loading-screen quality; museum/mission rendering; and whether the second launch worked. Keep logs local until reviewed: they may contain account or machine details. Do not upload the bottle. Keep the test account until we finish reviewing the result.

The previous controlled isolation test denied Homebrew and development directories on this same host. A separate account complements that check but still shares system software installed for all users.
