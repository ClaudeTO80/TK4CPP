// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: adding, changing and removing the password of an archive.

#include <ZipWrapper/HighLevel.h>

#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    if (!Zip::Archive::encryptionSupported(Zip::Encryption::AES256)) {
        std::cout << "this libzip build has no AES support: nothing to demonstrate\n";
        return 0;
    }

    const fs::path dir = fs::temp_directory_path() / "zipwrapper_snippet_hl_protect";
    fs::remove_all(dir);
    fs::create_directories(dir / "project");
    { std::ofstream(dir / "project/notes.txt") << "top secret\n"; }
    const fs::path archive = dir / "notes.zip";
    Zip::archiveDirectory(dir / "project", archive);   // starts unprotected

    //! [protect]
    Zip::ProtectOptions add;
    add.newPassword = "first";                          // no old password: the archive was plain
    Zip::changeProtection(archive, add);                // in place, all or nothing

    Zip::ProtectOptions change;
    change.oldPassword = "first";
    change.newPassword = "second";
    Zip::changeProtection(archive, change);

    Zip::ProtectOptions remove;
    remove.oldPassword = "second";                      // no new password: protection is removed
    Zip::changeProtection(archive, remove);
    //! [protect]

    //! [copy]
    Zip::ProtectOptions copy;
    copy.newPassword = "for-the-auditor";
    copy.encryption = Zip::Encryption::AES128;
    copy.encryptOnly = {"*.txt"};                       // leave everything else readable
    Zip::changeProtection(archive, dir / "protected-copy.zip", copy);   // the input is untouched
    //! [copy]

    const auto summary = Zip::summarize(dir / "protected-copy.zip");
    const bool ok = Zip::summarize(archive).encryptedEntries == 0 && summary.encryptedEntries == 1 &&
                    Zip::readText(archive, "notes.txt") == "top secret\n";
    fs::remove_all(dir);
    return ok ? 0 : 1;
}
