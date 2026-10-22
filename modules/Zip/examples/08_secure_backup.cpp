// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

/**
 * @file 08_secure_backup.cpp
 * @brief A complete backup tool built only on the high-level API.
 *
 * Usage:
 *   secure_backup pack   <dir> <out.zip> [--include P]... [--exclude P]...
 *                        [--encrypt-only P]... [--overwrite]
 *   secure_backup list   <archive.zip>
 *   secure_backup cat    <archive.zip> <entry>
 *   secure_backup unpack <archive.zip> <dest> [--include P]... [--exclude P]...
 *                        [--overwrite]
 *   secure_backup verify <archive.zip>
 *   secure_backup passwd <archive.zip> [--remove]
 *
 * Passwords come from the environment, never from argv:
 *   ZIP_PASSWORD      encrypts on `pack`, decrypts on the other commands,
 *                     and is the NEW password for `passwd`
 *   ZIP_OLD_PASSWORD  the current password for `passwd`
 *
 * Demonstrates: archiveDirectory(), listEntries()/formatListing(),
 * readBytes(), extractArchive(), verifyArchive(), changeProtection(),
 * passwordFromEnv(), progress reporting and cancellation-safe output.
 */

#include <ZipWrapper/HighLevel.h>

#include <iostream>

namespace {

struct Cli {
    std::vector<std::string> positional;
    std::vector<std::string> include;
    std::vector<std::string> exclude;
    std::vector<std::string> encryptOnly;
    bool overwrite = false;
    bool remove = false;
};

/** @brief Parse `--flag value` / `--switch` arguments after the command. */
bool parse(int argc, char** argv, Cli& cli) {
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](std::vector<std::string>& into) {
            if (i + 1 >= argc) return false;
            into.emplace_back(argv[++i]);
            return true;
        };
        if (a == "--include") { if (!next(cli.include)) return false; }
        else if (a == "--exclude") { if (!next(cli.exclude)) return false; }
        else if (a == "--encrypt-only") { if (!next(cli.encryptOnly)) return false; }
        else if (a == "--overwrite") cli.overwrite = true;
        else if (a == "--remove") cli.remove = true;
        else if (a.starts_with("--")) return false;
        else cli.positional.push_back(a);
    }
    return true;
}

/** @brief Single-line percentage on stderr, only when it changes. */
struct Bar {
    int last = -1;
    void show(double fraction) {
        const int pct = static_cast<int>(fraction * 100.0);
        if (pct == last) return;
        last = pct;
        std::cerr << "\r  " << pct << "%" << std::flush;
        if (pct >= 100) std::cerr << '\n';
    }
};

int usage() {
    std::cerr << "usage: secure_backup pack|list|cat|unpack|verify|passwd ... "
                 "(see the header of this file)\n";
    return 2;
}

int pack(const Cli& cli) {
    if (cli.positional.size() != 2) return usage();
    Zip::ArchiveOptions options;
    options.filter.include = cli.include;
    options.filter.exclude = cli.exclude;
    options.encryptOnly = cli.encryptOnly;
    options.password = Zip::passwordFromEnv();     // encrypts only when set
    options.overwriteOutput = cli.overwrite;
    options.verifyAfterWrite = true;               // trust, but verify
    Bar bar;
    options.progress = [&](double f) { bar.show(f); return true; };

    const auto r = Zip::archiveDirectory(cli.positional[0], cli.positional[1], options);
    std::cout << r.files << " files, " << r.directories << " directories, "
              << r.inputBytes << " -> " << r.archiveBytes << " bytes";
    if (options.password) std::cout << ", " << r.encrypted << " encrypted (contents only)";
    std::cout << '\n';
    for (const auto& w : r.warnings) std::cout << "warning: " << w << '\n';
    for (const auto& s : r.skipped) std::cout << "skipped: " << s << '\n';
    if (options.password)
        std::cout << "note: names, sizes and dates stay visible without the password\n";
    return 0;
}

int list(const Cli& cli) {
    if (cli.positional.size() != 1) return usage();
    const auto entries = Zip::listEntries(cli.positional[0]);   // never needs a password
    std::cout << Zip::formatListing(entries);
    if (Zip::summarize(entries).needsPassword())
        std::cout << "a password is required to read the encrypted entries\n";
    return 0;
}

int cat(const Cli& cli) {
    if (cli.positional.size() != 2) return usage();
    // Same call for plain and encrypted archives: the password is only used if needed.
    const auto data = Zip::readBytes(cli.positional[0], cli.positional[1], Zip::passwordFromEnv());
    std::cout.write(reinterpret_cast<const char*>(data.data()),
                    static_cast<std::streamsize>(data.size()));
    return 0;
}

int unpack(const Cli& cli) {
    if (cli.positional.size() != 2) return usage();
    Zip::UnpackOptions options;
    options.filter.include = cli.include;
    options.filter.exclude = cli.exclude;
    options.overwrite = cli.overwrite;
    options.password = Zip::passwordFromEnv();
    options.continueOnError = true;                 // report every problem, not just the first

    const auto r = Zip::extractArchive(cli.positional[0], cli.positional[1], options);
    std::cout << r.extracted << " extracted";
    if (r.filtered) std::cout << ", " << r.filtered << " filtered out";
    std::cout << ", " << r.failures.size() << " failed\n";
    for (const auto& f : r.failures) std::cout << "  FAIL " << f.name << ": " << f.message << '\n';
    return r.ok() ? 0 : 1;
}

int verify(const Cli& cli) {
    if (cli.positional.size() != 1) return usage();
    const auto r = Zip::verifyArchive(cli.positional[0], Zip::passwordFromEnv());
    for (const auto& p : r.problems) std::cout << "  BAD  " << p << '\n';
    std::cout << r.checked << " verified, " << r.skipped << " not verified (need a password), "
              << r.problems.size() << " problems\n";
    return r.ok() ? 0 : 1;
}

int passwd(const Cli& cli) {
    if (cli.positional.size() != 1) return usage();
    Zip::ProtectOptions options;
    options.oldPassword = Zip::passwordFromEnv("ZIP_OLD_PASSWORD");
    if (!cli.remove) {
        options.newPassword = Zip::passwordFromEnv();
        if (!options.newPassword) {
            std::cerr << "set ZIP_PASSWORD to the new password, or use --remove\n";
            return 2;
        }
    }
    // Written next to the original and swapped in only when complete.
    Zip::changeProtection(cli.positional[0], options);
    std::cout << (cli.remove ? "password removed\n" : "password changed\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string command = argv[1];
    Cli cli;
    if (!parse(argc, argv, cli)) return usage();

    try {
        if (command == "pack") return pack(cli);
        if (command == "list") return list(cli);
        if (command == "cat") return cat(cli);
        if (command == "unpack") return unpack(cli);
        if (command == "verify") return verify(cli);
        if (command == "passwd") return passwd(cli);
        return usage();
    } catch (const Zip::Exception& e) {
        std::cerr << "error (" << e.zipCode() << "): " << e.what() << '\n';
        return 1;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "filesystem error: " << e.what() << '\n';
        return 1;
    }
}
