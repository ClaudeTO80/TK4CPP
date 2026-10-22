// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Claudio

// Documentation snippet: the pattern rules, each example checked for real.

#include <ZipWrapper/HighLevel.h>

#include <iostream>

static int failures = 0;

static void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAILED: " << what << '\n';
    }
}

#define CHECK(expr) check((expr), #expr)

int main() {
    //! [exclude]
    Zip::PathFilter drop;
    drop.exclude = {"node_modules", "*.tmp", "build/cache", "file?.txt"};

    CHECK(drop.excludes("node_modules/x/i.js"));         // a name without '/' matches any path component:
    CHECK(drop.excludes("a/b/node_modules"));            //   the whole subtree, at any depth
    CHECK(!drop.excludes("my_node_modules/x"));          //   but only whole components
    CHECK(drop.excludes("a/b.tmp"));                     // '*' stays inside one component
    CHECK(drop.excludes("build/cache/x.o"));             // a pattern with '/' is anchored at the root ...
    CHECK(!drop.excludes("other/build/cache"));          //   so this is not matched
    CHECK(drop.excludes("file1.txt"));                   // '?' is exactly one character
    CHECK(!drop.excludes("file10.txt"));
    //! [exclude]

    //! [include]
    Zip::PathFilter take;
    take.include = {"*.cpp", "docs/**"};

    CHECK(take.includes("src/main.cpp", false));         // a name without '/' matches the file name, at any depth
    CHECK(!take.includes("main.cpp.bak", false));
    CHECK(take.includes("docs/deep/guide.md", false));   // '**' is any number of directories, including none
    CHECK(!take.includes("src", true));                  // directories never match a non-empty include list

    Zip::PathFilter deep;
    deep.include = {"src/**/*.cpp"};
    CHECK(deep.includes("src/main.cpp", false));         // '**' may match zero directories
    CHECK(deep.includes("src/util/helper.cpp", false));
    CHECK(!deep.includes("srcx/a.cpp", false));
    //! [include]

    //! [combine]
    Zip::PathFilter both;
    both.include = {"*.cpp"};
    both.exclude = {"tests"};
    CHECK(both.accepts("src/a.cpp", false));
    CHECK(!both.accepts("tests/a.cpp", false));          // exclude wins
    //! [combine]

    std::cout << (failures ? "pattern examples FAILED\n" : "pattern examples ok\n");
    return failures ? 1 : 0;
}
